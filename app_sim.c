/**
 * @file app_sim.c
 * @brief Theo dõi rút / cắm SIM không cần URC (SIM hot swap fallback) -- NASA Tracking
 *
 * Luồng xử lý (gọi SIM_Poll từ SmsReceiverTask):
 * 1. Mỗi SIM_POLL_SEC giây hỏi trạng thái SIM (+CPIN).
 * 2. SIM_ABSENT_CONFIRM lần liên tiếp không sẵn sàng -> coi là rút SIM.
 * 3. Rút SIM quá SIM_REDETECT_AFTER_SEC: mỗi SIM_REDETECT_INTERVAL_SEC giây
 *    tắt / bật sóng (CFUN 0 -> 1) để module đọc lại khe SIM.
 * 4. SIM sẵn sàng trở lại, hoặc ICCID đổi trong lúc vẫn sẵn sàng (đổi SIM giữa
 *    2 lần hỏi) -> cấu hình lại SMS, báo NASA đăng nhập lại với ICCID mới.
 *
 * Lưu ý: File này KHÔNG #include simcom_api.h trực tiếp.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_net.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_sms.h"
#include "app_nasa.h"
#include "app_sim.h"
#include <string.h>

static uint32_t s_lastPollTick = 0;
static uint32_t s_lastIccidTick = 0;
static uint32_t s_absentSinceTick = 0;
static uint32_t s_lastRedetectTick = 0;
static int      s_ready = -1;      /* -1 = chưa biết (vừa khởi động) */
static int      s_notReadyCount = 0;
static int      s_lockedLogged = 0;
static char     s_iccid[32] = {0};

int SIM_IsReady(void)
{
    return (s_ready == 1) ? 1 : 0;
}

static uint32_t ElapsedSec(uint32_t sinceTick, uint32_t nowTick)
{
    return (nowTick - sinceTick) / HAL_TICKS_PER_SEC;
}

/** SIM vừa sẵn sàng (lần đầu / cắm lại) hoặc vừa đổi: cập nhật ICCID, SMS, NASA */
static void OnSimReady(const char *reason)
{
    char iccid[32] = {0};

    if (HAL_NET_GetIccid(iccid, sizeof(iccid)) != 0) {
        /* ICCID chưa đọc được ngay sau khi SIM sẵn sàng: thử lại ở lần hỏi sau */
        HAL_LOG("[SIM] %s: ready but ICCID not readable yet", reason);
        return;
    }

    HAL_LOG("[SIM] %s: ready ICCID=%s (old=%s)", reason, iccid, s_iccid[0] ? s_iccid : "-");
    s_ready = 1;
    s_notReadyCount = 0;
    s_absentSinceTick = 0;
    s_lockedLogged = 0;

    if (strcmp(iccid, s_iccid) != 0) {
        strncpy(s_iccid, iccid, sizeof(s_iccid) - 1);
        s_iccid[sizeof(s_iccid) - 1] = '\0';
        NASA_OnSimChanged(s_iccid);
    }

    /* SIM mới / vừa cắm lại: CNMI phải cấu hình lại mới nhận được SMS */
    if (SMS_EnsureReady() != 0) {
        HAL_LOG("[SIM] SMS re-config fail (will work once modem ready)");
    }
}

static void RedetectSim(void)
{
    HAL_LOG("[SIM] absent %lus -> radio off/on (CFUN 0/1) to re-detect SIM",
            (unsigned long)ElapsedSec(s_absentSinceTick, GetTickNow()));
    if (HAL_NET_SetRadio(0) != 0) HAL_LOG("[SIM] CFUN=0 fail");
    HAL_OS_TaskSleep(SIM_RADIO_OFF_SEC * HAL_TICKS_PER_SEC);
    if (HAL_NET_SetRadio(1) != 0) HAL_LOG("[SIM] CFUN=1 fail");
}

void SIM_Poll(void)
{
    uint32_t now = GetTickNow();
    int st;

    if (s_lastPollTick != 0 && ElapsedSec(s_lastPollTick, now) < SIM_POLL_SEC) return;
    s_lastPollTick = (now != 0) ? now : 1;

    st = HAL_NET_GetSimState();

    if (st == HAL_SIM_READY) {
        if (s_ready != 1) {
            OnSimReady((s_ready == -1) ? "boot" : "inserted");
            s_lastIccidTick = now;
        } else if (ElapsedSec(s_lastIccidTick, now) >= SIM_ICCID_CHECK_SEC) {
            /* Đổi SIM nhanh giữa 2 lần hỏi: trạng thái vẫn READY nhưng ICCID khác */
            char iccid[32] = {0};
            s_lastIccidTick = now;
            if (HAL_NET_GetIccid(iccid, sizeof(iccid)) == 0 && strcmp(iccid, s_iccid) != 0) {
                OnSimReady("swapped");
            }
        }
        return;
    }

    if (st == HAL_SIM_LOCKED) {
        /* Cần PIN / PUK: tắt bật sóng không giúp được */
        if (!s_lockedLogged) {
            HAL_LOG("[SIM] SIM locked (PIN/PUK required)");
            s_lockedLogged = 1;
        }
        s_ready = 0;
        return;
    }

    /* Không có SIM / lỗi SIM / chưa trả lời */
    if (s_notReadyCount < SIM_ABSENT_CONFIRM) s_notReadyCount++;
    if (s_notReadyCount < SIM_ABSENT_CONFIRM) return;

    if (s_ready != 0) {
        HAL_LOG("[SIM] not ready (state=%d) -> treat as removed", st);
        s_ready = 0;
        s_absentSinceTick = (now != 0) ? now : 1;
        s_lastRedetectTick = 0;
    }
    if (s_absentSinceTick == 0) s_absentSinceTick = (now != 0) ? now : 1;

    if (ElapsedSec(s_absentSinceTick, now) >= SIM_REDETECT_AFTER_SEC &&
        (s_lastRedetectTick == 0 ||
         ElapsedSec(s_lastRedetectTick, now) >= SIM_REDETECT_INTERVAL_SEC)) {
        RedetectSim();
        s_lastRedetectTick = GetTickNow();
        /* Cho module thời gian khởi động lại SIM trước lần hỏi kế tiếp */
        s_lastPollTick = s_lastRedetectTick;
    }
}
