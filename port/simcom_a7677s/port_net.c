/**
 * @file    port_net.c
 * @brief   HAL_NET: PDP, socket TCP, song, NITZ -- SIMCom A7677S OpenSDK
 *
 * Chi cac file trong port/ duoc goi sAPI_*. Ham SDK dung o day: SDK_API_LIST.md.
 */

#include "port_sdk.h"

#ifndef sAPI_htons
#define sAPI_htons(n) ((uint16_t)((((uint16_t)(n) & 0xFF00) >> 8) | (((uint16_t)(n) & 0x00FF) << 8)))
#endif

/* ========================== HAL_NET ======================================= */

static INT32 s_netSocketFd = -1;

void HAL_NET_Init(void)
{
    sAPI_NetworkInit();
}

void HAL_NET_NitzEnable(void)
{
    sAPI_NetworkSetCtzu(1);
}

int HAL_NET_GetImei(char *outImei, uint32_t bufLen)
{
    if (!outImei || bufLen == 0) return -1;
    if (sAPI_SysGetImei(outImei) == 0) return 0;
    return -1;
}

int HAL_NET_GetIccid(char *outIccid, uint32_t bufLen)
{
    if (!outIccid || bufLen == 0) return -1;
    if (sAPI_SysGetIccid(outIccid) == SC_SIM_RETURN_SUCCESS) return 0;
    return -1;
}

int HAL_NET_GetSimState(void)
{
    UINT8 cpin = SC_SIM_UNKNOW;

    if (sAPI_SimcardPinGet(&cpin) != SC_SIM_RETURN_SUCCESS) return HAL_SIM_UNKNOWN;
    switch (cpin) {
    case SC_SIM_READY:
        return HAL_SIM_READY;
    case SC_SIM_PIN:
    case SC_SIM_PUK:
    case SC_SIM_BLOCKED:
        return HAL_SIM_LOCKED;
    case SC_SIM_REMOVED:
    case SC_SIM_CRASH:
    case SC_SIM_NOINSRETED:
        return HAL_SIM_ABSENT;
    default:
        return HAL_SIM_UNKNOWN;
    }
}

int HAL_NET_SetRadio(int on)
{
    return (sAPI_NetworkSetCfun(on ? SC_CFUN_1 : SC_CFUN_0) == SC_NET_SUCCESS) ? 0 : -1;
}

int HAL_NET_SetFlightMode(int on)
{
    int ok = (sAPI_NetworkSetCfun(on ? SC_CFUN_4 : SC_CFUN_1) == SC_NET_SUCCESS);
    HAL_LOG_Print("[NET] CFUN=%d %s", on ? 4 : 1, ok ? "OK" : "fail");
    return ok ? 0 : -1;
}

int HAL_NET_ActivatePdp(int pdpId)
{
    return (sAPI_TcpipPdpActive(pdpId, 1) == 0) ? 0 : -1;
}

int HAL_NET_Connect(const char *host, int port)
{
    SCsockAddrIn addr;
    INT32 fd;

    if (s_netSocketFd >= 0) {
        sAPI_TcpipClose(s_netSocketFd);
        s_netSocketFd = -1;
    }

    fd = sAPI_TcpipSocket(SC_AF_INET, SC_SOCK_STREAM, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = SC_AF_INET;
    addr.sin_port = sAPI_htons((UINT16)port);
    addr.sin_addr.s_addr = sAPI_TcpipInetAddr((INT8 *)host);

    if (sAPI_TcpipConnect(fd, (SCsockAddr *)&addr, sizeof(addr)) != 0) {
        sAPI_TcpipClose(fd);
        return -1;
    }

    s_netSocketFd = fd;
    return 0;
}

void HAL_NET_Disconnect(void)
{
    if (s_netSocketFd >= 0) {
        sAPI_TcpipClose(s_netSocketFd);
        s_netSocketFd = -1;
    }
}

int HAL_NET_Send(const uint8_t *data, uint32_t len)
{
    if (s_netSocketFd < 0 || !data) return -1;
    return (int)sAPI_TcpipSend(s_netSocketFd, (void *)data, (INT32)len, 0);
}

extern int lwip_getsockerrno(int s);

int HAL_NET_Recv(uint8_t *buf, uint32_t bufLen, uint32_t timeoutUs)
{
    (void)timeoutUs;
    if (s_netSocketFd < 0 || !buf) return -1;

    int ret = (int)sAPI_TcpipRecv(s_netSocketFd, buf, (INT32)bufLen, SC_MSG_DONTWAIT);
    if (ret < 0) {
        int err = lwip_getsockerrno(s_netSocketFd);
        /* 11 = EAGAIN / EWOULDBLOCK (Không có dữ liệu mới trên non-blocking socket) */
        if (err == 11 || err == 0) {
            return 0;
        }
        return -1;
    }
    return ret;
}

int HAL_NET_IsConnected(void)
{
    return (s_netSocketFd >= 0) ? 1 : 0;
}

int HAL_NET_GetCsq(uint8_t *csq)
{
    if (!csq) return -1;
    sAPI_NetworkGetCsq((UINT8 *)csq);
    return 0;
}

void HAL_NET_SetNonBlocking(void)
{
    if (s_netSocketFd >= 0) {
        int flags = 1;
        sAPI_TcpipIoctlsocket(s_netSocketFd, SC_FIONBIO, &flags);
    }
}
