# API SDK SIMCom đang dùng (port/simcom_a7677s)

Danh sách đối chiếu khi nâng SDK: mở `sc_lib/<module>/inc/*.h` và `sc_lib/inc/*.h` của SDK mới, kiểm tra từng hàm / kiểu / hằng dưới đây còn tồn tại và **cùng chữ ký** (tham số, kiểu trả về, mã lỗi). Chỗ nào khác thì chỉ sửa file `port_*.c` tương ứng; `app/` và `hal/` không đổi.

Đã kiểm tra với: A7677S_MANV_1606_V702_OPENSDK (07/2024) và A7677S_MANV_1606_V702_OPENSDK_ENORLFSV2 (11/2024): 84 hàm cùng chữ ký. Tổng cộng 84 hàm `sAPI_*` (cộng `ApiMapInit`).

Tạo lại danh sách (Git Bash, trong thư mục này):

```bash
sed -E 's#//.*##' port_*.c | grep -oE '\bsAPI_[A-Za-z0-9_]+' | sort -u
```

## Header SDK

Gom trong [port_sdk.h](port_sdk.h), include tường minh từng file (không dựa vào `simcom_api.h`, vì bản 11/2024 đã bỏ bớt include): `simcom_tcpip_old.h` (socket), `simcom_os.h`, `simcom_debug.h`, `simcom_common.h`, `simcom_usb_vcom.h`, `sc_power.h`, `simcom_simcard.h`, `simcom_tcpip.h`, `simcom_network.h`, `simcom_system.h`, `simcom_gpio.h`, `sc_i2c.h`, `sc_spi.h`, `sc_app_update.h`, `sc_app_download.h`, `simcom_fota_download.h`, `sc_flash.h`, `simcom_rtc.h`, `simcom_wtd.h`, `simcom_gps.h`, `simcom_loc.h`, `simcom_file.h`, `simcom_sms.h`, `simcom_at.h`. Riêng [port_entry.c](port_entry.c) dùng `api_map.h`.

## Theo file

| File | Hàm SDK | Kiểu / hằng SDK |
|---|---|---|
| [port_entry.c](port_entry.c) | `ApiMapInit` | `userSpaceEntry_t`, section `.userSpaceRegTable` |
| [port_log.c](port_log.c) | `sAPI_Debug`, `sAPI_AtSend`, `sAPI_UsbVcomRead`, `sAPI_UsbVcomWrite`, `sAPI_UsbVcomRegisterCallbackEX`, `sAPI_FlagCreate`, `sAPI_FlagSet`, `sAPI_FlagWait`, `sAPI_MutexCreate`, `sAPI_MutexLock`, `sAPI_MutexUnLock`, `sAPI_TaskCreate` | `sFlagRef`, `sMutexRef`, `sTaskRef`, `SC_FLAG_OR`, `SC_FLAG_OR_CLEAR`, `SC_FIFO`, `SC_SUSPEND`, `SC_SUCCESS` |
| [port_os.c](port_os.c) | `sAPI_TaskCreate`, `sAPI_TaskSleep`, `sAPI_GetTicks`, `sAPI_Malloc`, `sAPI_Free`, `sAPI_MutexCreate`, `sAPI_MutexLock`, `sAPI_MutexUnLock`, `sAPI_MsgQCreate`, `sAPI_MsgQSend`, `sAPI_MsgQRecv`, `sAPI_UrcRefRegister`, `sAPI_SysReset`, `sAPI_SoftWtdEnable`, `sAPI_SetWtdTimeOutPeriod`, `sAPI_FeedWtd`, `sAPI_FalutWakeEnable`, `sAPI_SystemSleepSet`, `sAPI_SystemSleepGet`, `sAPI_UsbVbusDetect`, `sAPI_GetPowerUpEvent`, `sAPI_GetPowerDownEvent` | `sTaskRef`, `sMutexRef`, `sMsgQRef`, `SIM_MSG_T`, `SC_STATUS`, `SC_SUCCESS`, `SC_FIFO`, `SC_SUSPEND`, `SC_NO_SUSPEND`, `SC_SYSTEM_SLEEP_ENABLE`, `SC_SYSTEM_SLEEP_DISABLE` |
| [port_periph.c](port_periph.c) | `sAPI_GpioConfig`, `sAPI_GpioSetValue`, `sAPI_GpioGetValue`, `sAPI_GpioWakeupEnable`, `sAPI_SpiConfigInitEx`, `sAPI_SpiWriteBytesEx`, `sAPI_SpiReadBytesEx`, `sAPI_SpiCustomCsSetLevel`, `sAPI_I2CConfigInit`, `sAPI_I2CRead`, `sAPI_I2CWrite`, `sAPI_ReadAdc`, `sAPI_SystemSleepSet` | `SC_GPIOConfiguration`, `SC_GPIOReturnCode`, `SC_GPIOTransitionType`, `SC_GPIO_*` (IN/OUT, PULL*, *_EDGE), `SC_GPIORC_*`, `SC_SPI_DEV`, `SC_SPI_ReturnCode`, `SC_SPI_RC_OK`, `SC_I2C_DEV`, `SC_I2C_CHANNEL`, `SC_I2C_STANDARD_MODE`, `SC_I2C_FAST_MODE`, `SC_I2C_RC_OK` |
| [port_net.c](port_net.c) | `sAPI_NetworkInit`, `sAPI_NetworkSetCfun`, `sAPI_NetworkSetCtzu`, `sAPI_NetworkGetCsq`, `sAPI_SimcardPinGet`, `sAPI_SysGetImei`, `sAPI_SysGetIccid`, `sAPI_TcpipPdpActive`, `sAPI_TcpipSocket`, `sAPI_TcpipConnect`, `sAPI_TcpipSend`, `sAPI_TcpipRecv`, `sAPI_TcpipClose`, `sAPI_TcpipIoctlsocket`, `sAPI_TcpipInetAddr`; `lwip_getsockerrno` (khai báo `extern`) | `SC_AF_INET`, `SC_SOCK_STREAM`, `SC_FIONBIO`, `SC_MSG_DONTWAIT`, `SC_NET_SUCCESS`, `SC_CFUN_0/1/4`, `SC_SIM_*` (READY, PIN, PUK, BLOCKED, REMOVED, CRASH, NOINSRETED, UNKNOW, RETURN_SUCCESS). `sAPI_htons` dùng bản của SDK nếu có, không thì macro trong file |
| [port_gnss.c](port_gnss.c) | `sAPI_GnssPowerStatusSet`, `sAPI_GnssPowerStatusGet`, `sAPI_GnssStartMode`, `sAPI_GnssModeSet`, `sAPI_GnssModeGet`, `sAPI_GnssInfoGet`, `sAPI_SendCmd2Gnss`, `sAPI_GnssAgpsSeviceOpen`, `sAPI_LocGet`, `sAPI_GetRealTimeClock`, `sAPI_SetRealTimeClock`, `sAPI_MsgQCreate`, `sAPI_MsgQSend`, `sAPI_MsgQRecv`, `sAPI_UrcRefRegister`, `sAPI_TaskCreate`, `sAPI_TaskSleep`, `sAPI_GetTicks`, `sAPI_Free` | `SC_Gnss_Mode`, `SC_Gnss_Return_Code`, `SC_GNSS_MODE_GPS_BDS_QZSS` … `SC_GNSS_MODE_GPS_QZSS`, `SC_GNSS_POWER_ON/OFF`, `SC_GNSS_START_HOT`, `SC_URC_GNSS_MASK`, `SC_lbs_info_t`, `SC_LBS_RETURNCODE`, `SC_LBS_SUCCESS`, `SC_SRV_LBS`, `t_rtc`, `SIM_MSG_T`, `sMsgQRef`, `sTaskRef` |
| [port_sms.c](port_sms.c) | `sAPI_SmsSetFormat`, `sAPI_SmsSetNewMsgInd`, `sAPI_SmsReadMsg`, `sAPI_SmsSendMsg`, `sAPI_SmsDelOneMsg`, `sAPI_MsgQCreate`, `sAPI_MsgQRecv`, `sAPI_UrcRefRegister`, `sAPI_Free` | `SC_SMSReturnCode`, `SC_SMS_SUCESS`, `SC_URC_SMS_MASK`, `SC_URC_NEW_MSG_IND`, `SC_URC_FLASH_MSG`, `SIM_MSG_T`, `sMsgQRef`, `SC_STATUS`, `SC_SUCCESS` |
| [port_fs_fota.c](port_fs_fota.c) | `sAPI_fopen`, `sAPI_fread`, `sAPI_fwrite`, `sAPI_fseek`, `sAPI_ftell`, `sAPI_fclose`, `sAPI_remove`, `sAPI_AppDownload`, `sAPI_AppPackageCrc`, `sAPI_FotaServiceBegin`, `sAPI_FBF_Disable` | `SC_FotaApiParam`, `sc_fota_callback`, `SC_APP_DOWNLOAD_HTTP_MOD`, `SC_APP_DOWNLOAD_FTP_MOD` |

## Ngoài hàm SDK

- Chuỗi URC GNSS (`+CGNSSINFO`, …) do SDK đưa lên được [app/app_gps.c](../../app/app_gps.c) tự phân tích: nếu SDK mới đổi định dạng dòng URC thì phải sửa cả bộ phân tích trong app.
- Số pad GPIO, kênh SPI / I2C / ADC: [board_pins.h](board_pins.h).
- Link script, `CMakeLists.txt` gốc, `config/`: theo SDK mới; chỉ thêm lại dòng `INCLUDE(${CMAKE_SOURCE_DIR}/app/app_sources.cmake)` (xem [app_sources.cmake](../../app/app_sources.cmake)).
