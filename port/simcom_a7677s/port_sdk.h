/**
 * @file    port_sdk.h
 * @brief   Header noi bo cua port SIMCom A7677S: gom header SDK + HAL + libc
 *
 * Chi cac file port/simcom_a7677s/port_*.c include file nay. Ung dung (app/)
 * KHONG include -- app chi thay cac header hal_xxx.h.
 */
#ifndef PORT_SDK_H
#define PORT_SDK_H

/*
 * Header SDK: include tuong minh tung file, KHONG dua vao simcom_api.h de keo
 * theo. simcom_api.h cua SDK ENORLFSV2 (11/2024) da bo phan lon cac include
 * (tcpip, simcard, rtc, loc, fota...), ban cu thi co. Thu tu theo simcom_api.h
 * cua SDK cu: simcom_tcpip_old.h dung dau. Hai SDK deu co du cac file nay.
 */
#include "simcom_api.h"
#include "simcom_tcpip_old.h"  /* sAPI_Tcpip* socket, SC_AF_INET, SCsockAddr... */
#include "simcom_os.h"         /* task, mutex, msgq, flag, tick, malloc */
#include "simcom_debug.h"      /* sAPI_Debug */
#include "simcom_common.h"     /* SIM_MSG_T, URC mask, sAPI_UrcRefRegister */
#include "simcom_usb_vcom.h"   /* USB COM, sAPI_UsbVbusDetect */
#include "sc_power.h"          /* reset, ADC, power up / down event */
#include "simcom_simcard.h"    /* CPIN, ICCID */
#include "simcom_tcpip.h"      /* sAPI_TcpipPdpActive */
#include "simcom_network.h"    /* CFUN, CSQ, CTZU */
#include "simcom_system.h"     /* IMEI, sleep */
#include "simcom_gpio.h"
#include "sc_i2c.h"
#include "sc_spi.h"
#include "sc_app_update.h"     /* sAPI_AppPackageCrc */
#include "sc_app_download.h"   /* sAPI_AppDownload */
#include "simcom_fota_download.h"
#include "sc_flash.h"          /* sAPI_FBF_Disable */
#include "simcom_rtc.h"        /* t_rtc */
#include "simcom_wtd.h"
#include "simcom_gps.h"
#include "simcom_loc.h"        /* LBS */
#include "simcom_file.h"
#include "simcom_sms.h"

#include "hal_log.h"
#include "hal_os.h"
#include "hal_gpio.h"
#include "hal_net.h"
#include "hal_gnss.h"
#include "hal_spi.h"
#include "hal_i2c.h"
#include "hal_fs.h"
#include "hal_sms.h"
#include "hal_adc.h"
#include "hal_fota.h"
#include "hal_usbcom.h"
#include "simcom_at.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#endif /* PORT_SDK_H */
