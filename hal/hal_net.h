/**
 * @file    hal_net.h
 * @brief   HAL - Network/TCP Abstraction
 */
#ifndef HAL_NET_H
#define HAL_NET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void HAL_NET_Init(void);
void HAL_NET_NitzEnable(void);
int  HAL_NET_ActivatePdp(int pdpId);
int  HAL_NET_Connect(const char *host, int port);
void HAL_NET_Disconnect(void);
int  HAL_NET_Send(const uint8_t *data, uint32_t len);
int  HAL_NET_Recv(uint8_t *buf, uint32_t bufLen, uint32_t timeoutUs);
int  HAL_NET_IsConnected(void);
int  HAL_NET_GetCsq(uint8_t *csq);
void HAL_NET_SetNonBlocking(void);
int  HAL_NET_GetImei(char *outImei, uint32_t bufLen);
int  HAL_NET_GetIccid(char *outIccid, uint32_t bufLen);

#ifdef __cplusplus
}
#endif

#endif /* HAL_NET_H */
