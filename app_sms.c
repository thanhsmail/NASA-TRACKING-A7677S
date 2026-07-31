/**
 * @file app_sms.c
 * @brief SMS management module -- NASA Tracking
 *
 * Luu y: File nay KHONG #include simcom_api.h / simcom_common.h truc tiep.
 * Moi tuong tac SMS di qua HAL_SMS_*.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_sms.h"
#include "app_config.h"
#include "app_cmd.h"
#include "app_sms.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* CNMI (1,2,1,0,0,0) → SC_URC_NEW_MSG_IND + index; khớp SmsReceiverTask */
/* CNMI (1,2,1,0,0,0) -> SC_URC_NEW_MSG_IND + index; khop SmsReceiverTask */
static int SMS_ConfigureModem(void)
{
    if (HAL_SMS_SetFormat(1) != 0) {
        HAL_LOG("[SMS] SetFormat(1) fail");
        return -1;
    }

    if (HAL_SMS_SetNewMsgInd(1, 2, 1, 0, 0, 0) != 0) {
        HAL_LOG("[SMS] SetNewMsgInd fail");
        return -1;
    }

    HAL_LOG("[SMS] CNMI text+store OK");
    return 0;
}

void SMS_Init(void)
{
    /* Co the fail neu SIM chua ready -- SmsReceiverTask se retry */
    (void)SMS_ConfigureModem();
}

int SMS_EnsureReady(void)
{
    int i;
    for (i = 0; i < 10; i++) {
        if (SMS_ConfigureModem() == 0) return 0;
        HAL_OS_TaskSleep(HAL_TICKS_PER_SEC);
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


typedef struct {
    char phone[20];
} SmsReplyCtx;

static void SmsReply(const char *reply, void *ctx)
{
    SmsReplyCtx *c = (SmsReplyCtx *)ctx;

    if (!reply) return;
    HAL_LOG("[SMS/Reply] phone=%s msg=%s",
            (c && c->phone[0]) ? c->phone : "(none)", reply);

    if (!c || !c->phone[0]) return;

    if (HAL_SMS_Send(c->phone, reply) != 0) {
        HAL_LOG("[SMS/Reply] SendMsg fail");
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
        HAL_LOG("[SMS] body from %s phone=%s: %s", source, ctx.phone, body);
        CMD_Execute(body, source, ctx.phone, SmsReply, &ctx);
    }
}
