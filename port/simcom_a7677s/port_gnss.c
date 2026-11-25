/**
 * @file    port_gnss.c
 * @brief   HAL_GNSS: gnss_task (URC GNSS), che do ve tinh, AGPS, RTC, LBS -- SIMCom A7677S OpenSDK
 *
 * Chi cac file trong port/ duoc goi sAPI_*. Ham SDK dung o day: SDK_API_LIST.md.
 */

#include "port_sdk.h"

/* ========================== HAL_GNSS ====================================== */

static volatile HalGnssLineCb_t s_gnssLineCb = NULL;

void HAL_GNSS_SetLineCallback(HalGnssLineCb_t cb)
{
    s_gnssLineCb = cb;
}

int HAL_GNSS_GetRtc(HalDateTime_t *rtc)
{
    t_rtc r;
    if (!rtc) return -1;
    if (sAPI_GetRealTimeClock(&r) == 0) {
        rtc->tm_year = (uint16_t)r.tm_year;
        rtc->tm_mon  = (uint8_t)r.tm_mon;
        rtc->tm_mday = (uint8_t)r.tm_mday;
        rtc->tm_hour = (uint8_t)r.tm_hour;
        rtc->tm_min  = (uint8_t)r.tm_min;
        rtc->tm_sec  = (uint8_t)r.tm_sec;
        return 0;
    }
    return -1;
}

int HAL_GNSS_SetRtc(const HalDateTime_t *rtc)
{
    t_rtc r;
    if (!rtc) return -1;
    memset(&r, 0, sizeof(r));
    r.tm_year = rtc->tm_year;
    r.tm_mon  = rtc->tm_mon;
    r.tm_mday = rtc->tm_mday;
    r.tm_hour = rtc->tm_hour;
    r.tm_min  = rtc->tm_min;
    r.tm_sec  = rtc->tm_sec;
    return (sAPI_SetRealTimeClock(&r) == 0) ? 0 : -1;
}

static sMsgQRef s_gnssUrcMsgQ = NULL;
static sTaskRef s_gnssTaskRef = NULL;
static uint8_t  s_gnssTaskStack[1024 * 4];

/*
 * Che do ve tinh: chip Unicorecomm ban trong nuoc, SDK ban nay chi co
 * 1 = GPS+BeiDou+QZSS, 2 = BeiDou, 3 = GPS+QZSS (khong co Galileo/GLONASS).
 */
static volatile int s_gnssWantMode = SC_GNSS_MODE_GPS_BDS_QZSS;
static volatile int s_gnssStarted = 0;

static int GnssApplyMode(void)
{
    SC_Gnss_Mode cur = sAPI_GnssModeGet();
    SC_Gnss_Return_Code rc;

    if ((int)cur == s_gnssWantMode) {
        HAL_LOG_Print("[GNSS] mode %d", (int)cur);
        return 0;
    }
    rc = sAPI_GnssModeSet((SC_Gnss_Mode)s_gnssWantMode);
    HAL_LOG_Print("[GNSS] mode %d -> %d rc=%d now=%d",
                  (int)cur, s_gnssWantMode, (int)rc, (int)sAPI_GnssModeGet());
    return (rc == 0) ? 0 : -1;
}

int HAL_GNSS_SetMode(int mode)
{
    if (mode < SC_GNSS_MODE_GPS_BDS_QZSS || mode > SC_GNSS_MODE_GPS_QZSS) return -1;
    s_gnssWantMode = mode;
    if (!s_gnssStarted) return 0; /* ap dung khi task GNSS bat chip */
    return GnssApplyMode();
}

int HAL_GNSS_GetMode(void)
{
    if (!s_gnssStarted) return -1;
    return (int)sAPI_GnssModeGet();
}

/* Che do giu tinh (A76xx GNSS Application Note: chong troi khi dung yen) */
static volatile int s_gnssStaticHoldCms = -1; /* -1 = khong gui lenh, de mac dinh chip */

static int GnssApplyStaticHold(void)
{
    char cmd[40];
    SC_Gnss_Return_Code rc;

    if (s_gnssStaticHoldCms < 0) return 0;
    snprintf(cmd, sizeof(cmd), "$CFGDYN,h02,1,%d", s_gnssStaticHoldCms);
    rc = sAPI_SendCmd2Gnss(cmd);
    HAL_LOG_Print("[GNSS] %s rc=%d", cmd, (int)rc);
    return (rc == 0) ? 0 : -1;
}

int HAL_GNSS_SetStaticHold(int cmPerSec)
{
    if (cmPerSec < 0 || cmPerSec > 1000) return -1;
    s_gnssStaticHoldCms = cmPerSec;
    if (!s_gnssStarted) return 0;
    return GnssApplyStaticHold();
}

/* AGPS: sAPI_GnssAgpsSeviceOpen mo socket toi may chu AGNSS va cho, nen chay
 * trong task rieng de khong chan task GNSS (mat URC) hay task NASA (watchdog) */
static sTaskRef s_agpsTaskRef = NULL;
static uint8_t  s_agpsTaskStack[1024 * 4];
static volatile int s_agpsRequested = 0;
static volatile int s_agpsBusy = 0;

static void sTask_Agps(void *argv)
{
    (void)argv;
    for (;;) {
        if (!s_agpsRequested) {
            /* GNSS tat (ngu sau) thi khong co yeu cau AGPS: cho 60 s / lan */
            sAPI_TaskSleep(s_gnssStarted ? 200 : 60 * 200);
            continue;
        }
        s_agpsRequested = 0;
        s_agpsBusy = 1;
        HAL_LOG_Print("[GNSS/AGPS] download... (GNSS power=%d)", (int)sAPI_GnssPowerStatusGet());
        {
            UINT32 t0 = sAPI_GetTicks();
            int rc = (int)sAPI_GnssAgpsSeviceOpen();
            UINT32 ms = (sAPI_GetTicks() - t0) * 5u; /* 200 tick/s */
            const char *txt =
                (rc == 0)   ? "OK" :
                (rc == -1)  ? "GNSS power off" :
                (rc == 101) ? "open socket fail" :
                (rc == 102) ? "fetch AGNSS server fail" :
                (rc == 103) ? "connect AGNSS server fail" :
                (rc == 104) ? "socket write fail" :
                (rc == 105) ? "socket read fail" :
                (rc == 106) ? "AGNSS data error" :
                (rc == 107) ? "send data to GNSS chip fail" : "unknown";
            HAL_LOG_Print("[GNSS/AGPS] done rc=%d (%s) in %lu ms", rc, txt, (unsigned long)ms);
        }
        s_agpsBusy = 0;
    }
}

int HAL_GNSS_AgpsRequest(void)
{
    if (!s_gnssStarted || s_agpsBusy || s_agpsRequested) return -1;
    if (s_agpsTaskRef == NULL) {
        if (sAPI_TaskCreate(&s_agpsTaskRef, s_agpsTaskStack, sizeof(s_agpsTaskStack),
                            25, "agps_task", sTask_Agps, NULL) != SC_SUCCESS) {
            s_agpsTaskRef = NULL;
            HAL_LOG_Print("[GNSS/AGPS] create task fail");
            return -1;
        }
    }
    s_agpsRequested = 1;
    return 0;
}

/* LBS: sAPI_LocGet mo ket noi toi may chu LBS cua SIMCom va tra ket qua qua
 * message queue (toi ~9 s), nen chay trong task rieng nhu AGPS */
static sTaskRef s_lbsTaskRef = NULL;
static uint8_t  s_lbsTaskStack[1024 * 4];
static sMsgQRef s_lbsRespQ = NULL;
static volatile int      s_lbsRequested = 0;
static volatile int      s_lbsBusy = 0;
static volatile uint32_t s_lbsTimeoutSec = 30;
static volatile int      s_lbsHasResult = 0;
static HalLbsResult_t    s_lbsResult;

static void LbsDrainQueue(void)
{
    SIM_MSG_T stale;
    for (;;) {
        memset(&stale, 0, sizeof(stale));
        if (sAPI_MsgQRecv(s_lbsRespQ, &stale, SC_NO_SUSPEND) != SC_SUCCESS) break;
        if (stale.arg3) sAPI_Free(stale.arg3);
    }
}

static void LbsQueryOnce(HalLbsResult_t *res)
{
    SIM_MSG_T msg;
    SC_LBS_RETURNCODE rc;
    UINT32 t0 = sAPI_GetTicks();
    UINT32 waitTicks = s_lbsTimeoutSec * 200u;

    memset(res, 0, sizeof(*res));
    LbsDrainQueue(); /* ket qua tre cua lan hoi truoc (da het gio) */
    rc = sAPI_LocGet(0, s_lbsRespQ, 1);
    if (rc != SC_LBS_SUCCESS) {
        res->errCode = -1;
        HAL_LOG_Print("[LBS] sAPI_LocGet rc=%d", (int)rc);
        return;
    }
    for (;;) {
        UINT32 used = sAPI_GetTicks() - t0;
        if (used >= waitTicks) {
            res->errCode = -2;
            HAL_LOG_Print("[LBS] timeout %lus", (unsigned long)s_lbsTimeoutSec);
            return;
        }
        memset(&msg, 0, sizeof(msg));
        if (sAPI_MsgQRecv(s_lbsRespQ, &msg, waitTicks - used) != SC_SUCCESS) continue;
        if (msg.msg_id != SC_SRV_LBS || msg.arg3 == NULL) {
            if (msg.arg3) sAPI_Free(msg.arg3);
            continue;
        }
        {
            SC_lbs_info_t *info = (SC_lbs_info_t *)msg.arg3;
            res->errCode = (int)info->u8ErrorCode;
            if (info->u8ErrorCode == 0) {
                /* SDK tra do * 1e6; ep int32 de giu dau (Nam / Tay) */
                res->lat = (double)(int32_t)info->u32Lat / 1000000.0;
                res->lon = (double)(int32_t)info->u32Lng / 1000000.0;
                res->accM = (int)info->u16Acc;
                res->ok = 1;
            }
            HAL_LOG_Print("[LBS] ret=%d lat=%.6f lon=%.6f acc=%dm in %lu ms",
                          res->errCode, res->lat, res->lon, res->accM,
                          (unsigned long)((sAPI_GetTicks() - t0) * 5u));
            sAPI_Free(msg.arg3);
        }
        return;
    }
}

static void sTask_Lbs(void *argv)
{
    HalLbsResult_t res;
    (void)argv;
    for (;;) {
        if (!s_lbsRequested) {
            sAPI_TaskSleep(200);
            continue;
        }
        s_lbsRequested = 0;
        LbsQueryOnce(&res);
        s_lbsResult = res;
        s_lbsHasResult = 1;
        s_lbsBusy = 0;
    }
}

int HAL_LBS_Request(uint32_t timeoutSec)
{
    if (s_lbsBusy || s_lbsRequested) return -1;
    if (s_lbsRespQ == NULL) {
        if (sAPI_MsgQCreate(&s_lbsRespQ, "lbs_respq", sizeof(SIM_MSG_T), 4, SC_FIFO) != SC_SUCCESS) {
            s_lbsRespQ = NULL;
            HAL_LOG_Print("[LBS] create msgq fail");
            return -1;
        }
    }
    if (s_lbsTaskRef == NULL) {
        if (sAPI_TaskCreate(&s_lbsTaskRef, s_lbsTaskStack, sizeof(s_lbsTaskStack),
                            25, "lbs_task", sTask_Lbs, NULL) != SC_SUCCESS) {
            s_lbsTaskRef = NULL;
            HAL_LOG_Print("[LBS] create task fail");
            return -1;
        }
    }
    s_lbsTimeoutSec = (timeoutSec < 10u) ? 10u : timeoutSec;
    s_lbsHasResult = 0;
    s_lbsBusy = 1;
    s_lbsRequested = 1;
    return 0;
}

int HAL_LBS_GetResult(HalLbsResult_t *out)
{
    if (!s_lbsHasResult || out == NULL) return 0;
    *out = s_lbsResult;
    s_lbsHasResult = 0;
    return 1;
}

/* Giam sat GNSS: chip gui +CGNSSINFO moi giay (ke ca khi khong co fix), nen
 * im lang lau = chip / driver GNSS ngung: tat roi bat lai GNSS */
static volatile int      s_gnssNoUrcRestartSec = 60;
static volatile uint32_t s_gnssLastUrcTick = 0;
static volatile uint32_t s_gnssTaskAliveTick = 0;
static int               s_gnssRestartCount = 0;

/** Bat GNSS va cau hinh lai tu dau (dung luc khoi dong va khi khoi phuc) */
static void GnssPowerOnAndConfigure(void)
{
    /* Trinh tu theo A76xx GNSS Application Note: bat nguon 1 lan (bat 2 lan lien
     * tiep lam hong nap dong), moi lenh cach nhau ~100 ms de chip kip tra loi */
    sAPI_GnssPowerStatusSet(SC_GNSS_POWER_ON);
    sAPI_TaskSleep(100);
    s_gnssStarted = 1;
    HAL_LOG_Print("[GNSS] hot start rc=%d", (int)sAPI_GnssStartMode(SC_GNSS_START_HOT));
    sAPI_TaskSleep(20);
    (void)GnssApplyMode();
    sAPI_TaskSleep(20);
    (void)GnssApplyStaticHold();
    sAPI_TaskSleep(20);
    sAPI_GnssInfoGet(1); /* Bat URC dinh ky 1s (+CGNSSINFO) */
}

static void GnssRestart(uint32_t silentSec)
{
    s_gnssRestartCount++;
    HAL_LOG_Print("[GNSS] no URC for %lus -> restart GNSS (#%d)",
                  (unsigned long)silentSec, s_gnssRestartCount);
    sAPI_GnssInfoGet(0);
    s_gnssStarted = 0;
    sAPI_GnssPowerStatusSet(SC_GNSS_POWER_OFF);
    sAPI_TaskSleep(400); /* 2 s cho chip tat han */
    GnssPowerOnAndConfigure();
}

/* Tat / bat GNSS theo yeu cau (ngu tiet kiem ac quy): chi gnss_task goi lenh chip */
static volatile int s_gnssWantOn = 1;

static void GnssPowerOff(void)
{
    sAPI_GnssInfoGet(0);
    s_gnssStarted = 0;
    sAPI_GnssPowerStatusSet(SC_GNSS_POWER_OFF);
}

void HAL_GNSS_SetPower(int on)
{
    s_gnssWantOn = on ? 1 : 0;
    /* gnss_task co the dang cho 60 s (GNSS tat): gui tin rong cho no bat ngay */
    if (on && s_gnssUrcMsgQ) {
        SIM_MSG_T msg;
        memset(&msg, 0, sizeof(msg));
        (void)sAPI_MsgQSend(s_gnssUrcMsgQ, &msg);
    }
}

int HAL_GNSS_IsPowered(void)
{
    return s_gnssStarted ? 1 : 0;
}

int HAL_GNSS_SetNoUrcRestartSec(int sec)
{
    if (sec < 10) return -1;
    s_gnssNoUrcRestartSec = sec;
    return 0;
}

uint32_t HAL_GNSS_GetTaskAliveAgeSec(void)
{
    uint32_t alive = s_gnssTaskAliveTick;
    uint32_t now = sAPI_GetTicks();
    if (alive == 0 || now < alive) return 0; /* chua chay / vua cap nhat */
    return (now - alive) / 200u;
}

static void sTask_GnssUrcListener(void *argv)
{
    SIM_MSG_T msg;
    (void)argv;

    HAL_LOG("[GNSS] Task Listener Start");
    GnssPowerOnAndConfigure();
    s_gnssLastUrcTick = sAPI_GetTicks();

    while (1) {
        uint32_t now;
        memset(&msg, 0, sizeof(msg));
        /* Cho toi da 5 s thay vi cho mai, de con kiem tra GNSS im lang. GNSS tat
         * (ngu sau) thi 60 s, van duoi GNSS_TASK_HANG_SEC; HAL_GNSS_SetPower(1)
         * gui tin rong de thuc som. Vua yeu cau bat thi 1 s */
        if (sAPI_MsgQRecv(s_gnssUrcMsgQ, &msg,
                          (s_gnssStarted ? 5u : (s_gnssWantOn ? 1u : 60u)) * 200u) == SC_SUCCESS) {
            if (msg.arg3) {
                const char *str = (const char *)msg.arg3;
                HalGnssLineCb_t cb = s_gnssLineCb;
                HAL_LOG("[GNSS/URC] %s", str);
                if (cb) cb(str);
                sAPI_Free(msg.arg3);
                s_gnssLastUrcTick = sAPI_GetTicks();
                if (s_gnssRestartCount > 0) {
                    HAL_LOG_Print("[GNSS] URC back after %d restart(s)", s_gnssRestartCount);
                    s_gnssRestartCount = 0;
                }
            }
        }

        now = sAPI_GetTicks();
        s_gnssTaskAliveTick = (now != 0) ? now : 1;

        if (!s_gnssWantOn) {
            /* Doi AGPS tai xong moi tat (AGPS can GNSS dang bat) */
            if (s_gnssStarted && !s_agpsBusy && !s_agpsRequested) {
                HAL_LOG_Print("[GNSS] power off (sleep)");
                GnssPowerOff();
            }
            s_gnssLastUrcTick = now; /* tat co chu dich: khong tinh la im lang */
            continue;
        }
        if (!s_gnssStarted) {
            HAL_LOG_Print("[GNSS] power on (wake)");
            GnssPowerOnAndConfigure();
            s_gnssLastUrcTick = sAPI_GetTicks();
            continue;
        }

        if ((now - s_gnssLastUrcTick) >= (uint32_t)s_gnssNoUrcRestartSec * 200u) {
            GnssRestart((now - s_gnssLastUrcTick) / 200u);
            s_gnssLastUrcTick = sAPI_GetTicks();
        }
    }
}

void HAL_GNSS_UrcListenerStart(void)
{
    if (s_gnssUrcMsgQ == NULL) {
        if (sAPI_MsgQCreate(&s_gnssUrcMsgQ, "gnss_urcq", sizeof(SIM_MSG_T), 20, SC_FIFO) == SC_SUCCESS) {
            sAPI_UrcRefRegister(s_gnssUrcMsgQ, SC_URC_GNSS_MASK);
            sAPI_TaskCreate(&s_gnssTaskRef, s_gnssTaskStack, sizeof(s_gnssTaskStack), 10, "gnss_task", sTask_GnssUrcListener, NULL);
        }
    }
}
