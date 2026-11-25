/**
  ******************************************************************************
  * @file    cus_usb_vcom.c
  * @author  SIMCom OpenSDK Team
  * @brief   Source file of USB Virtual port comunication,it is for USB AT port read\write.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 SIMCom Wireless.
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/

#include "userspaceConfig.h"
#include "simcom_common.h"
#include "simcom_debug.h"
#include "stdlib.h"
#include "simcom_usb_vcom.h"
#include "stdio.h"
#include "string.h"
#include "simcom_at.h"
#include "pub_cache.h"
#include "sal_os.h"
#include "sal_log.h"

#define LOG(...)       sal_log(__VA_ARGS__)
#define LOG_ERROR(...) sal_log_error("[DEMO-INIT] " __VA_ARGS__)
#define LOG_INFO(...)  sal_log_info("[DEMO-INIT] " __VA_ARGS__)
#define LOG_TRACE(...) sal_log_trace("[DEMO-INIT] " __VA_ARGS__)

#define USB_VCOM_RX_BUFFER_SIZE     512

#define SCFW_ATHEAD              ("AT")
#define SCFW_ATTAIL              ("\r\n")

#define DEMO_USB_CACHE_SIZE SAL_4K


pub_cache_p g_usb_cache;
bool g_usb_inited;

/**
  * @brief  Determine whether the input data is an AT command.
  */
int isATCommand(void* data, int length)
{
    char *ret;

    if(strncmp((char *)data, SCFW_ATHEAD, 2) == 0)
    {
        ret = strstr(data, SCFW_ATTAIL);
        if(ret != NULL)
        {
            return 1;
        }
    }

    return 0;
}

/**
  * @brief  Callback for USB VCOM receiving event,customer could read out the data with pointer usbVCcomData.If define SIMCOM_UI_DEMO_TO_USB_AT_PORT then send message to UI demo task.
  * @param  Pointer para.
  * @note   In real application please loop reading by sAPI_UsbVcomRead until return value of sAPI_UsbVcomRead is 0.
  * @retval void
  */
void UsbVcomCBFunc(void *para)
{
    int readLen = 0;
    char *usbVCcomData = malloc(USB_VCOM_RX_BUFFER_SIZE);
    // char *usbVCcomData = sal_malloc(USB_VCOM_RX_BUFFER_SIZE);

    memset(usbVCcomData,0,USB_VCOM_RX_BUFFER_SIZE);
    readLen = sAPI_UsbVcomRead((UINT8 *)usbVCcomData, USB_VCOM_RX_BUFFER_SIZE);
    sAPI_Debug("%s, usbVCcomData = %s,readLen[%d]",__func__,usbVCcomData,readLen);


#ifdef SIMCOM_UI_DEMO_TO_USB_AT_PORT

    int savelen = 0;
    if (g_usb_cache)
        savelen = pub_cache_write(g_usb_cache, usbVCcomData, readLen);

    if (savelen != readLen)
    {
        LOG_ERROR("drop data size: %d, readsize:%d, savesize:%d",
                  readLen - savelen,
                  readLen,
                  savelen);
    }



#endif
    free((void*)usbVCcomData);

    return;
}

/**
  * @brief  Extension Callback for USB VCOM receiving event with fixed data length.If define SIMCOM_UI_DEMO_TO_USB_AT_PORT then send message to UI demo task.
  * @param  Pointer para.
  * @note   In real application please loop reading by sAPI_UsbVcomRead until return value of sAPI_UsbVcomRead is 0.
  * @retval void
  */
void UsbVcomCBFuncEx(int len, void *para)
{
    int readLen = 0;
    unsigned char *usbVCcomData = malloc(len);

    memset(usbVCcomData,0,len);
    readLen = sAPI_UsbVcomRead((UINT8 *)usbVCcomData, len);
    if(sAPI_UsbVbusDetect() == 1)
    {
        sAPI_Debug("%s, usbVCcomData = %s,readLen[%d]",__func__,usbVCcomData,readLen);
        if( isATCommand(usbVCcomData,readLen) == 1)
        {
            // sAPI_AtSend((UINT8 *)usbVCcomData,readLen);//send AT command
            sAPI_UsbVcomWrite((UINT8 *)usbVCcomData, readLen);
        }
        else
        {
#ifdef SIMCOM_UI_DEMO_TO_USB_AT_PORT
    int savelen = 0;
    if (g_usb_cache)
        savelen = pub_cache_write(g_usb_cache, (char *)usbVCcomData, readLen);

    if (savelen != readLen)
    {
        LOG_ERROR("drop data size: %d, readsize:%d, savesize:%d",
                  readLen - savelen,
                  readLen,
                  savelen);
    }            

#endif            
        }
    }

    free((void*)usbVCcomData);

    return;
}

/**
  * @brief  Create USB VCOM task and register receiving callback.
  * @param  void
  * @note   Default with UsbVcomCBFuncEx callback.
  * @retval void
  */
void sAPP_UsbVcomTask(void)
{
    g_usb_cache = pub_cache_create(DEMO_USB_CACHE_SIZE, CACHE_TYPE_BUFFER, CACHE_SYNC_R);
    if (!g_usb_cache)
    {
        LOG_ERROR("usb cache create fail");
        return ;
    }
    g_usb_inited = true;
    sAPI_UsbVcomRegisterCallbackEX(UsbVcomCBFuncEx, (void *)"UsbVcomCBFuncEx");
    sAPI_Debug("sAPP_UsbVcomTask Succeed!\n");
}


