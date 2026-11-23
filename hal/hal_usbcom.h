/**
 * @file    hal_usbcom.h
 * @brief   HAL - Cong COM ao qua USB (USB AT port cua module)
 *
 * Sau HAL_USBCOM_Open() du lieu PC gui vao cong USB AT duoc chuyen cho app
 * (khong con di vao bo xu ly lenh AT cua module). Du lieu nhan duoc chep vao
 * vong dem trong callback cua SDK; task app doc ra bang HAL_USBCOM_Read().
 */
#ifndef HAL_USBCOM_H
#define HAL_USBCOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Dang ky nhan du lieu tu cong USB COM. @return 0 = OK, -1 = loi */
int HAL_USBCOM_Open(void);

/**
 * Doc toi da len byte. Cho toi timeoutTicks neu chua co du lieu
 * (HAL_OS_WAIT_FOREVER = cho mai).
 * @return so byte doc duoc (0 = het thoi gian cho)
 */
int HAL_USBCOM_Read(uint8_t *buf, uint32_t len, uint32_t timeoutTicks);

/**
 * Chuyen mot dong lenh AT (khong kem CR LF) cho bo xu ly AT cua module;
 * ket qua AT tu tra ve cong USB AT. @return 0 = OK, -1 = loi
 */
int HAL_USBCOM_AtSend(const char *line);

/** So lan callback nhan, len SDK bao va so byte doc duoc lan cuoi (chan doan) */
void HAL_USBCOM_GetStats(uint32_t *cbCount, int *lastLen, int *lastRead);

/** Gui du lieu ra cong USB COM. @return 0 = OK, -1 = loi */
int HAL_USBCOM_Write(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_USBCOM_H */
