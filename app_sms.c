#include "simcom_api.h"
#include "simcom_common.h"
#include "app_config.h"
#include "app_cmd.h"
#include "app_sms.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Giữ API cũ để tương thích; config thật nằm ở app_cfg */

static sMsgQRef s_smsSendRspQ = NULL;

/* CNMI (1,2,1,0,0,0) → SC_URC_NEW_MSG_IND + index; khớp SmsReceiverTask */
static int SMS_ConfigureModem(void)
{
    SC_SMSReturnCode ret;

    ret = sAPI_SmsSetFormat(1); /* text mode */
    if (ret != SC_SMS_SUCESS) {
        sAPI_Debug("[SMS] SetFormat(1) fail=%d", (int)ret);
        return -1;
    }

    /* mode,mt,bm,ds,bfr = 1,2,1,0,0 — lưu SIM, báo index qua URC */
    ret = sAPI_SmsSetNewMsgInd(1, 2, 1, 0, 0, 0);
    if (ret != SC_SMS_SUCESS) {
        sAPI_Debug("[SMS] SetNewMsgInd fail=%d", (int)ret);
        return -1;
    }

    sAPI_Debug("[SMS] CNMI text+store OK");
    return 0;
}

void SMS_Init(void)
{
    /* Có thể fail nếu SIM chưa ready — SmsReceiverTask sẽ retry */
    (void)SMS_ConfigureModem();
}

int SMS_EnsureReady(void)
{
    int i;
    for (i = 0; i < 10; i++) {
        if (SMS_ConfigureModem() == 0) return 0;
        sAPI_TaskSleep(SC_TICKS_PER_SECOND);
    }
    return -1;
}

int SMS_IsFotaDownloadReady(void)
{
    return CMD_IsFotaReady();
}

void SMS_SetFotaDownloadHandled(void)
{
    CMD_SetFotaHandled();
}

int SMS_GetPeriodMoving(void) { return 0; } /* deprecated — dùng CFG_ */
int SMS_GetPeriodStopped(void) { return 0; }
const char *SMS_GetLicensePlate(void) { return ""; }
const char *SMS_GetDriverName(void) { return ""; }
const char *SMS_GetDriverLicense(void) { return ""; }
void SMS_SetLicensePlate(const char *val) { (void)val; }
void SMS_SetDriverName(const char *val) { (void)val; }
void SMS_SetDriverLicense(const char *val) { (void)val; }

typedef struct {
    char phone[20];
} SmsReplyCtx;

static void SmsReply(const char *reply, void *ctx)
{
    SmsReplyCtx *c = (SmsReplyCtx *)ctx;
    SC_SMSReturnCode ret;

    if (!reply) return;
    sAPI_Debug("[SMS/Reply] phone=%s msg=%s",
               (c && c->phone[0]) ? c->phone : "(none)", reply);

    if (!c || !c->phone[0]) return;

    if (s_smsSendRspQ == NULL) {
        if (sAPI_MsgQCreate(&s_smsSendRspQ, "smsSendRspQ",
                            sizeof(SIM_MSG_T), 4, SC_FIFO) != SC_SUCCESS) {
            sAPI_Debug("[SMS/Reply] create msgQ fail");
            s_smsSendRspQ = NULL;
            return;
        }
    }

    ret = sAPI_SmsSendMsg(1, (UINT8 *)reply, (UINT16)strlen(reply),
                          (UINT8 *)c->phone, s_smsSendRspQ);
    if (ret != SC_SMS_SUCESS) {
        sAPI_Debug("[SMS/Reply] SendMsg fail=%d", (int)ret);
        return;
    }
    {
        /* 2000 tick = 10s; 15000 tick (75s) chặn queue SMS quá lâu gây tràn */
        SIM_MSG_T rsp = {0};
        if (sAPI_MsgQRecv(s_smsSendRspQ, &rsp, 2000) == SC_SUCCESS) {
            if (rsp.arg3) sAPI_Free(rsp.arg3);
        }
    }
}

void SMS_Command_Execute(const char *body, const char *src)
{
    CMD_Execute(body, src, NULL, NULL, NULL);
}

/* Chuỗi trong nháy có phải số điện thoại không: bắt đầu '+' hoặc chữ số,
 * còn lại toàn chữ số, dài >= 8 ký tự số */
static int LooksLikePhone(const char *s)
{
    int digits = 0;
    if (!s || !s[0]) return 0;
    if (*s == '+') s++;
    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        digits++;
        s++;
    }
    return (digits >= 8) ? 1 : 0;
}

static void ExtractPhoneFromPayload(const char *payload, char *out, int outsz)
{
    /*
     * Response +CMGR/+CMT có nhiều chuỗi trong nháy, chuỗi đầu có thể là
     * "REC UNREAD" — duyệt tất cả và chọn chuỗi trông giống số điện thoại.
     */
    const char *p;
    if (!payload || !out || outsz <= 0) return;
    out[0] = '\0';

    p = payload;
    while ((p = strchr(p, '"')) != NULL) {
        char tmp[24];
        int i = 0;
        p++;
        while (*p && *p != '"' && i < (int)sizeof(tmp) - 1) {
            tmp[i++] = *p++;
        }
        tmp[i] = '\0';
        if (*p == '"') p++;

        if (LooksLikePhone(tmp)) {
            strncpy(out, tmp, outsz - 1);
            out[outsz - 1] = '\0';
            return;
        }
    }
}

void SMS_ExtractAndExecuteSmsBody(const char *payload, const char *source)
{
    SmsReplyCtx ctx;
    const char *p_body;
    char body[256];
    char *end;

    if (!payload) return;
    memset(&ctx, 0, sizeof(ctx));
    ExtractPhoneFromPayload(payload, ctx.phone, sizeof(ctx.phone));

    p_body = strchr(payload, '\n');
    if (!p_body) p_body = payload;
    else p_body++;

    while (*p_body == '\r' || *p_body == '\n' || *p_body == ' ' || *p_body == '\t')
        p_body++;

    strncpy(body, p_body, sizeof(body) - 1);
    body[sizeof(body) - 1] = '\0';

    end = body + strlen(body);
    while (end > body && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';

    if (body[0] != '\0') {
        sAPI_Debug("[SMS] body from %s phone=%s: %s", source, ctx.phone, body);
        CMD_Execute(body, source, ctx.phone, SmsReply, &ctx);
    }
}
