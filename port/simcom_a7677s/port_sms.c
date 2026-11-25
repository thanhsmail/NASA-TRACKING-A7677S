/**
 * @file    port_sms.c
 * @brief   HAL_SMS: cau hinh modem SMS, URC tin nhan, doc / gui / xoa -- SIMCom A7677S OpenSDK
 *
 * Chi cac file trong port/ duoc goi sAPI_*. Ham SDK dung o day: SDK_API_LIST.md.
 */

#include "port_sdk.h"

/* ========================== HAL_SMS ======================================= */

static sMsgQRef s_smsRspQ = NULL;

static void DrainSmsRspQ(void)
{
    SIM_MSG_T stale;
    while (1) {
        memset(&stale, 0, sizeof(stale));
        if (sAPI_MsgQRecv(s_smsRspQ, &stale, SC_NO_SUSPEND) != SC_SUCCESS) break;
        if (stale.arg3) sAPI_Free(stale.arg3);
    }
}

int HAL_SMS_SetFormat(int format)
{
    SC_SMSReturnCode ret = sAPI_SmsSetFormat(format);
    return (ret == SC_SMS_SUCESS) ? 0 : -1;
}

int HAL_SMS_SetNewMsgInd(int mode, int mt, int bm, int ds, int bfr, int dummy)
{
    (void)dummy;
    SC_SMSReturnCode ret = sAPI_SmsSetNewMsgInd(mode, mt, bm, ds, bfr, 0);
    return (ret == SC_SMS_SUCESS) ? 0 : -1;
}

int HAL_SMS_Send(const char *phone, const char *text)
{
    SC_SMSReturnCode ret;
    if (!phone || !text) return -1;
    if (s_smsRspQ == NULL) {
        if (sAPI_MsgQCreate(&s_smsRspQ, "hal_sms_rspq", sizeof(SIM_MSG_T), 4, SC_FIFO) != SC_SUCCESS)
            return -1;
    }
    ret = sAPI_SmsSendMsg(1, (UINT8 *)text, (UINT16)strlen(text), (UINT8 *)phone, s_smsRspQ);
    if (ret == SC_SMS_SUCESS) {
        SIM_MSG_T rsp = {0};
        if (sAPI_MsgQRecv(s_smsRspQ, &rsp, 2000) == SC_SUCCESS) {
            if (rsp.arg3) sAPI_Free(rsp.arg3);
        }
        return 0;
    }
    return -1;
}

int HAL_SMS_EnsureReady(void)
{
    if (HAL_SMS_SetFormat(1) == 0 && HAL_SMS_SetNewMsgInd(1, 2, 1, 0, 0, 0) == 0) {
        return 0;
    }
    return -1;
}

int HAL_SMS_CreateUrcQueue(HalSmsRspQ_t *q)
{
    sMsgQRef urcQ = NULL;
    SC_STATUS st;

    if (s_smsRspQ == NULL) {
        st = sAPI_MsgQCreate(&s_smsRspQ, "hal_sms_rspq",
                             sizeof(SIM_MSG_T), 4, SC_FIFO);
        if (st != SC_SUCCESS) return -1;
    }

    st = sAPI_MsgQCreate(&urcQ, "hal_sms_urcq",
                         sizeof(SIM_MSG_T), 10, SC_FIFO);
    if (st != SC_SUCCESS) return -1;

    *q = (HalSmsRspQ_t)urcQ;
    return 0;
}

int HAL_SMS_RegisterUrc(HalSmsRspQ_t q, uint32_t mask)
{
    (void)mask;
    sAPI_UrcRefRegister((sMsgQRef)q, SC_URC_SMS_MASK);
    return 0;
}

int HAL_SMS_Read(int index, char *outBuf, uint32_t bufLen)
{
    SIM_MSG_T rsp = {0};
    if (!outBuf || !s_smsRspQ) return -1;

    DrainSmsRspQ();
    if (sAPI_SmsReadMsg(1, index, s_smsRspQ) != SC_SMS_SUCESS) return -1;
    if (sAPI_MsgQRecv(s_smsRspQ, &rsp, 1000) != SC_SUCCESS) return -1;

    if (rsp.arg3) {
        strncpy(outBuf, (const char *)rsp.arg3, bufLen - 1);
        outBuf[bufLen - 1] = '\0';
        sAPI_Free(rsp.arg3);
        return 0;
    }
    return -1;
}

int HAL_SMS_Delete(int index)
{
    SIM_MSG_T rsp = {0};
    if (!s_smsRspQ) return -1;

    DrainSmsRspQ();
    if (sAPI_SmsDelOneMsg(index, s_smsRspQ) != SC_SMS_SUCESS) return -1;
    if (sAPI_MsgQRecv(s_smsRspQ, &rsp, 1000) == SC_SUCCESS) {
        if (rsp.arg3) sAPI_Free(rsp.arg3);
    }
    return 0;
}

int HAL_SMS_RecvUrc(HalSmsRspQ_t q, HalSmsUrcEvent_t *evt, uint32_t timeoutTicks)
{
    SIM_MSG_T msg = {0};
    UINT32 tout;

    if (!evt) return -1;
    memset(evt, 0, sizeof(*evt));

    tout = (timeoutTicks == HAL_OS_WAIT_FOREVER) ? SC_SUSPEND :
           (timeoutTicks == HAL_OS_NO_WAIT)      ? SC_NO_SUSPEND :
           timeoutTicks;

    if (sAPI_MsgQRecv((sMsgQRef)q, &msg, tout) != SC_SUCCESS) return -1;
    if (msg.msg_id != SRV_URC) {
        if (msg.arg3) sAPI_Free(msg.arg3);
        return -1;
    }

    if (msg.arg2 == SC_URC_NEW_MSG_IND) {
        evt->type = HAL_SMS_URC_NEW_MSG;
        if (msg.arg3) {
            const char *p = strrchr((const char *)msg.arg3, ',');
            evt->index = p ? atoi(p + 1) : 0;
            sAPI_Free(msg.arg3);
        }
    } else if (msg.arg2 == SC_URC_FLASH_MSG) {
        evt->type = HAL_SMS_URC_FLASH_MSG;
        if (msg.arg3) {
            strncpy(evt->body, (const char *)msg.arg3, sizeof(evt->body) - 1);
            sAPI_Free(msg.arg3);
        }
    } else {
        if (msg.arg3) sAPI_Free(msg.arg3);
        return -1;
    }

    return 0;
}
