#include "simcom_api.h"
#include "simcom_common.h"
#include "simcom_tcpip.h"
#include "app_config.h"
#include "app_cmd.h"
#include "app_nasa.h"
#include "app_network.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

char *strtok_r(char *str, const char *delim, char **saveptr);

static INT32 s_tcpSocketFd = -1;
static int s_sockNonBlockSet = 0;

/* Buffer tích luỹ: TCP là stream, một dòng lệnh/ACK có thể bị chia nhiều segment */
static char s_rxAcc[NASA_SERVER_CMD_BUF_SIZE * 2];
static int  s_rxAccLen = 0;

static void ServerCmdReply(const char *reply, void *ctx)
{
    char out[400];
    int n;
    int ret;

    (void)ctx;
    if (!reply || s_tcpSocketFd < 0) return;

    n = snprintf(out, sizeof(out), "%s\r\n", reply);
    if (n <= 0) return;
    if (n >= (int)sizeof(out)) n = (int)sizeof(out) - 1;

    ret = Network_Send(out, (uint32_t)n);
    sAPI_Debug("[Network] Reply ret=%d len=%d: %s", ret, n, reply);
}

static void EnsureNonBlocking(void)
{
    UINT32 on = 1;
    if (s_tcpSocketFd < 0 || s_sockNonBlockSet) return;
    if (sAPI_TcpipIoctlsocket(s_tcpSocketFd, SC_FIONBIO, &on) == SC_SOCKET_ERROR) {
        sAPI_Debug("[Network] FIONBIO fail (continue with select)");
    } else {
        s_sockNonBlockSet = 1;
        sAPI_Debug("[Network] socket non-blocking OK");
    }
}

int Network_ActivatePdp(INT32 pdp_id)
{
    sAPI_Debug("[Network] Deactivating PDP %d prior to activation...", (int)pdp_id);
    (void)sAPI_TcpipPdpActive(pdp_id, 0); /* Hủy PDP context cũ kẹt do rớt sóng / tháo SIM */
    sAPI_TaskSleep(100);

    sAPI_Debug("[Network] Activating PDP %d...", (int)pdp_id);
    if (sAPI_TcpipPdpActive(pdp_id, 1) == SC_TCPIP_SUCCESS) {
        sAPI_Debug("[Network] PDP OK");
        return 0;
    }
    sAPI_Debug("[Network] PDP fail");
    return -1;
}

int Network_Connect(const char *host, int port)
{
    UINT32 ip;
    SCsockAddrIn srv;

    sAPI_Debug("[Network] Connect %s:%d", host, port);

    ip = sAPI_TcpipInetAddr((INT8 *)(void *)host);
    if (ip == 0xFFFFFFFFu) {
        SChostent *h = sAPI_TcpipGethostbyname((INT8 *)(void *)host);
        if (!h || !h->h_addr_list || !h->h_addr_list[0]) {
            sAPI_Debug("[Network] DNS fail");
            return -1;
        }
        ip = *(UINT32 *)h->h_addr_list[0];
    }

    if (s_tcpSocketFd >= 0) {
        sAPI_TcpipClose(s_tcpSocketFd);
        s_tcpSocketFd = -1;
    }
    s_sockNonBlockSet = 0;
    s_rxAccLen = 0;

    s_tcpSocketFd = sAPI_TcpipSocket(SC_AF_INET, SC_SOCK_STREAM, 0);
    if (s_tcpSocketFd < 0) return -1;

    memset(&srv, 0, sizeof(srv));
    srv.sin_family = SC_AF_INET;
    srv.sin_port = sAPI_TcpipHtons((UINT16)port);
    srv.sin_addr.s_addr = ip;

    if (sAPI_TcpipConnect(s_tcpSocketFd, (SCsockAddr *)&srv, sizeof(srv)) != 0) {
        sAPI_TcpipClose(s_tcpSocketFd);
        s_tcpSocketFd = -1;
        return -1;
    }

    EnsureNonBlocking();
    sAPI_Debug("[Network] Connected fd=%d", (int)s_tcpSocketFd);
    return 0;
}

void Network_Disconnect(void)
{
    if (s_tcpSocketFd >= 0) {
        sAPI_TcpipClose(s_tcpSocketFd);
        s_tcpSocketFd = -1;
    }
    s_sockNonBlockSet = 0;
    s_rxAccLen = 0;
}

int Network_Send(const char *data, uint32_t len)
{
    uint32_t sent = 0;
    int retry = 0;

    if (s_tcpSocketFd < 0 || !data || len == 0) return -1;

    /* Socket non-blocking: TcpipSend có thể trả partial / WOULDBLOCK */
    while (sent < len) {
        INT32 ret = sAPI_TcpipSend(s_tcpSocketFd,
                                   (INT8 *)(data + sent),
                                   (INT32)(len - sent), 0);
        if (ret > 0) {
            sent += (uint32_t)ret;
            retry = 0;
            continue;
        }
        if (++retry > 40) {
            /* Partial send = stream TCP đã hỏng khung — báo lỗi để caller
             * ngắt kết nối và đẩy bản ghi vào backup thay vì mất dữ liệu */
            sAPI_Debug("[Network] Send fail after retry sent=%u/%u",
                       (unsigned)sent, (unsigned)len);
            return -1;
        }
        sAPI_TaskSleep(5); /* ~25ms */
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
                sAPI_Debug("[Network] Route !NASA line");
                NASA_OnServerProtocolLine(line);
            } else {
                sAPI_Debug("[Network] Route CMD line: %s", line);
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
        /* Tràn buffer mà không có kết thúc dòng — dữ liệu bất thường, xả hết */
        sAPI_Debug("[Network] rx acc overflow (%d+%d), flush", s_rxAccLen, len);
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
    static char recvbuf[NASA_SERVER_CMD_BUF_SIZE];
    int recv_len;
    SCfdSet readfds;
    SCtimeval tv;
    int sel_ret;

    if (s_tcpSocketFd < 0) return 1;

    EnsureNonBlocking();

    /*
     * Ưu tiên recv non-blocking trực tiếp — trên một số firmware SIMCom,
     * select(timeout=0) + FD_ISSET đôi khi không báo dữ liệu dù đã có trong buffer.
     */
    recv_len = sAPI_TcpipRecv(s_tcpSocketFd, recvbuf, sizeof(recvbuf) - 1, 0);
    if (recv_len > 0) {
        recvbuf[recv_len] = '\0';
        sAPI_Debug("[Network] Recv %d bytes: %s", recv_len, recvbuf);
        AccumulateAndDispatch(recvbuf, recv_len);
        return 0;
    }
    if (recv_len == 0) {
        sAPI_Debug("[Network] peer closed");
        return 1;
    }

    /* recv_len < 0: thường là WOULDBLOCK, kiểm tra errno trước */
#ifdef EWOULDBLOCK
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return 0;
    }
#endif

    /* Fallback cho trường hợp errno không đồng bộ: select với timeout 0 (không block) */
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    SC_FD_ZERO(&readfds);
    SC_FD_SET(s_tcpSocketFd, &readfds);
    sel_ret = sAPI_TcpipSelect(s_tcpSocketFd + 1, &readfds, NULL, NULL, &tv);
    if (sel_ret == 0) {
        return 0; /* Không có dữ liệu, socket vẫn bình thường */
    }

    sAPI_Debug("[Network] recv/select err, sel_ret=%d, errno=%d", sel_ret, errno);
    return 1;
}

int Network_IsConnected(void)
{
    return (s_tcpSocketFd >= 0) ? 1 : 0;
}
