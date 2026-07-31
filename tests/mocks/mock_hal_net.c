/**
 * @file mock_hal_net.c
 * @brief Mock HAL Network implementation for Host Unit Testing
 */
#include "hal/hal_net.h"
#include <string.h>

static uint8_t s_mockCsq = 25;
static int s_mockConnected = 1;

void Mock_HAL_NET_SetCsq(uint8_t csq) { s_mockCsq = csq; }
void Mock_HAL_NET_SetConnected(int conn) { s_mockConnected = conn; }

int HAL_NET_GetCsq(uint8_t *csq)
{
    if (!csq) return -1;
    *csq = s_mockCsq;
    return 0;
}

int HAL_NET_GetImei(char *buf, size_t len)
{
    if (!buf || len == 0) return -1;
    strncpy(buf, "861385070676047", len - 1);
    buf[len - 1] = '\0';
    return 0;
}

int HAL_NET_GetIccid(char *buf, size_t len)
{
    if (!buf || len == 0) return -1;
    strncpy(buf, "89840810008783802230", len - 1);
    buf[len - 1] = '\0';
    return 0;
}

int HAL_NET_GetOperator(char *buf, size_t len)
{
    if (!buf || len == 0) return -1;
    strncpy(buf, "VIETTEL", len - 1);
    buf[len - 1] = '\0';
    return 0;
}

int HAL_NET_ActivatePdp(int pdpId) { (void)pdpId; return 0; }
int HAL_NET_DeactivatePdp(int pdpId) { (void)pdpId; return 0; }
int HAL_NET_IsPdpActive(int pdpId) { (void)pdpId; return 1; }

int HAL_NET_TcpConnect(HalSocketRef_t *sock, const char *host, uint16_t port)
{
    (void)host; (void)port;
    if (sock) *sock = (void*)0x1122;
    return s_mockConnected ? 0 : -1;
}

int HAL_NET_TcpSend(HalSocketRef_t sock, const void *data, uint32_t len)
{
    (void)sock; (void)data;
    return s_mockConnected ? (int)len : -1;
}

int HAL_NET_TcpRecv(HalSocketRef_t sock, void *buf, uint32_t maxLen, uint32_t timeoutTicks)
{
    (void)sock; (void)buf; (void)maxLen; (void)timeoutTicks;
    return 0;
}

void HAL_NET_TcpClose(HalSocketRef_t sock) { (void)sock; }
int HAL_NET_IsConnected(HalSocketRef_t sock) { (void)sock; return s_mockConnected; }
