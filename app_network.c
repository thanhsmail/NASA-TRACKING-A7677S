/**
 * @file app_network.c
 * @brief TCP Network module -- NASA Tracking
 *
 * Luu y: File nay KHONG #include simcom_api.h / simcom_tcpip.h truc tiep.
 * Moi tuong tac mang di qua HAL_NET_*.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_net.h"
#include "app_config.h"
#include "app_cmd.h"
#include "app_nasa.h"
#include "app_network.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

char *strtok_r(char *str, const char *delim, char **saveptr);

/*
 * Socket fd noi bo -- trang thai ket noi.
 * HAL_NET_* quan ly fd thuc, day chi track trang thai.
 */
static int s_isConnected = 0;

/* Buffer tích luỹ: TCP là stream, một dòng lệnh/ACK có thể bị chia nhiều segment */
static char s_rxAcc[NASA_SERVER_CMD_BUF_SIZE * 2];
static int  s_rxAccLen = 0;

static void DispatchRecvLines(char *recvbuf);

/* Server doi khi gui na,/sa, khong kem \r\n -- flush buffer con treo khi idle */
static void FlushAccAsCompleteLine(void)
{
    static char lineBuf[sizeof(s_rxAcc)];
    int len = s_rxAccLen;

    if (len <= 0) return;
    if (len >= (int)sizeof(lineBuf)) len = (int)sizeof(lineBuf) - 1;
    memcpy(lineBuf, s_rxAcc, (size_t)len);
    lineBuf[len] = '\0';
    s_rxAccLen = 0;
    s_rxAcc[0] = '\0';
    HAL_LOG("[Network] Flush unterminated CMD (%d): %s", len, lineBuf);
    DispatchRecvLines(lineBuf);
}

static void ServerCmdReply(const char *reply, void *ctx)
{
    char out[400];
    int n;
    int ret;

    (void)ctx;
    if (!reply || !s_isConnected) return;

    n = snprintf(out, sizeof(out), "%s\r\n", reply);
    if (n <= 0) return;
    if (n >= (int)sizeof(out)) n = (int)sizeof(out) - 1;

    ret = Network_Send(out, (uint32_t)n);
    HAL_LOG("[Network] Reply ret=%d len=%d: %s", ret, n, reply);
}

static void EnsureNonBlocking(void)
{
    /* Non-blocking duoc thiet lap trong HAL_NET_Connect qua HAL_NET_SetNonBlocking */
    HAL_NET_SetNonBlocking();
}

int Network_ActivatePdp(int pdp_id)
{
    HAL_LOG("[Network] Activating PDP %d...", pdp_id);
    if (HAL_NET_ActivatePdp(pdp_id) == 0) {
        HAL_LOG("[Network] PDP OK");
        return 0;
    }
    HAL_LOG("[Network] PDP fail");
    return -1;
}

int Network_Connect(const char *host, int port)
{
    HAL_LOG("[Network] Connect %s:%d", host, port);

    /* Dong ket noi cu neu con ton tai */
    if (s_isConnected) {
        HAL_NET_Disconnect();
        s_isConnected = 0;
        s_rxAccLen = 0;
    }

    if (HAL_NET_Connect(host, port) != 0) {
        HAL_LOG("[Network] Connect fail");
        return -1;
    }

    s_isConnected = 1;
    s_rxAccLen = 0;
    EnsureNonBlocking();
    HAL_LOG("[Network] Connected OK");
    return 0;
}

void Network_Disconnect(void)
{
    HAL_NET_Disconnect();
    s_isConnected = 0;
    s_rxAccLen = 0;
}

int Network_Send(const char *data, uint32_t len)
{
    uint32_t sent = 0;
    int retry = 0;

    if (!s_isConnected || !data || len == 0) return -1;

    /* Socket non-blocking: HAL_NET_Send co the tra partial / WOULDBLOCK */
    while (sent < len) {
        int ret = HAL_NET_Send((const uint8_t *)(data + sent), len - sent);
        if (ret > 0) {
            sent += (uint32_t)ret;
            retry = 0;
            continue;
        }
        if (++retry > 40) {
            /* Partial send -- stream TCP hong khung, caller ngat ket noi */
            HAL_LOG("[Network] Send fail after retry sent=%u/%u",
                    (unsigned)sent, (unsigned)len);
            return -1;
        }
        HAL_OS_TaskSleep(5); /* ~25ms */
    }
    return (int)len;
}

static void DispatchRecvLines(char *recvbuf)
{
    char *saveptr = NULL;
    char *line = strtok_r(recvbuf, "\r\n", &saveptr);

    while (line != NULL) {
        while (*line == ' ' || *line == '\t') line++;
        {
            char *end = line + strlen(line);
            while (end > line && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        }
        if (*line != '\0') {
            if (strncmp(line, "!NASA", 5) == 0) {
                HAL_LOG("[Network] Route !NASA line");
                NASA_OnServerProtocolLine(line);
            } else {
                HAL_LOG("[Network] Route CMD line: %s", line);
                CMD_Execute(line, "SERVER", NULL, ServerCmdReply, NULL);
            }
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
}

/*
 * Đẩy dữ liệu mới vào buffer tích luỹ và chỉ dispatch phần đã trọn dòng
 * (kết thúc bằng \r hoặc \n). Phần dư giữ lại chờ segment TCP kế tiếp.
 */
static void AccumulateAndDispatch(const char *data, int len)
{
    int i;
    int lastTerm;

    if (len > (int)sizeof(s_rxAcc) - 1 - s_rxAccLen) {
        /* Tran buffer -- du lieu bat thuong, xa het */
        HAL_LOG("[Network] rx acc overflow (%d+%d), flush", s_rxAccLen, len);
        s_rxAccLen = 0;
        if (len > (int)sizeof(s_rxAcc) - 1) len = (int)sizeof(s_rxAcc) - 1;
    }
    memcpy(s_rxAcc + s_rxAccLen, data, (size_t)len);
    s_rxAccLen += len;
    s_rxAcc[s_rxAccLen] = '\0';

    /* Tìm vị trí kết thúc dòng cuối cùng */
    lastTerm = -1;
    for (i = s_rxAccLen - 1; i >= 0; i--) {
        if (s_rxAcc[i] == '\n' || s_rxAcc[i] == '\r') {
            lastTerm = i;
            break;
        }
    }
    if (lastTerm < 0) return; /* chưa có dòng trọn vẹn */

    {
        /* static: chỉ task NASA gọi vào đây; tránh chiếm 2KB stack */
        static char lineBuf[sizeof(s_rxAcc)];
        int completeLen = lastTerm + 1;
        memcpy(lineBuf, s_rxAcc, (size_t)completeLen);
        lineBuf[completeLen] = '\0';

        /* Giữ lại phần dư (dòng chưa trọn) */
        s_rxAccLen -= completeLen;
        if (s_rxAccLen > 0) {
            memmove(s_rxAcc, s_rxAcc + completeLen, (size_t)s_rxAccLen);
        }
        s_rxAcc[s_rxAccLen] = '\0';

        DispatchRecvLines(lineBuf);
    }
}

int Network_RecvPoll(void)
{
    /* static: tranh chiem 1KB stack khi RecvPoll -> CMD -> CFG_Save long nhau */
    static char recvbuf[NASA_SERVER_CMD_BUF_SIZE];
    int recv_len;

    if (!s_isConnected) return 1;

    EnsureNonBlocking();

    /*
     * Trong SIMCom OpenSDK API sAPI_TcpipRecv:
     *   > 0 : So byte doc duoc
     *     0 : Res OK, khong co du lieu moi
     *   < 0 : Socket error / closed
     */
    recv_len = HAL_NET_Recv((uint8_t *)recvbuf, sizeof(recvbuf) - 1,
                             NASA_SERVER_RECV_TIMEOUT_US);
    if (recv_len > 0) {
        recvbuf[recv_len] = '\0';
        HAL_LOG("[Network] Recv %d bytes: %s", recv_len, recvbuf);
        AccumulateAndDispatch(recvbuf, recv_len);
        return 0;
    }

    if (recv_len == 0) {
        /* Binh thuong: Khong co byte mới, neu con dong lenh thieu \r\n thì flush */
        if (s_rxAccLen > 0)
            FlushAccAsCompleteLine();
        return 0;
    }

    /* recv_len < 0: Socket loi hoac ngat ket noi */
    HAL_LOG("[Network] Socket error/closed (%d)", recv_len);
    s_isConnected = 0;
    return 1;
}

int Network_IsConnected(void)
{
    return (HAL_NET_IsConnected() && s_isConnected) ? 1 : 0;
}
