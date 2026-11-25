/**
 * @file    port_fs_fota.c
 * @brief   HAL_FS, HAL_FOTA: file he thong noi bo, cap nhat firmware -- SIMCom A7677S OpenSDK
 *
 * Chi cac file trong port/ duoc goi sAPI_*. Ham SDK dung o day: SDK_API_LIST.md.
 */

#include "port_sdk.h"

/* ========================== HAL_FS ======================================== */

HalFile_t HAL_FS_Open(const char *path, const char *mode)
{
    SCFILE *f = sAPI_fopen(path, mode);
    return (HalFile_t)f;
}

int HAL_FS_Close(HalFile_t f)
{
    if (!f) return -1;
    return sAPI_fclose((SCFILE *)f) == 0 ? 0 : -1;
}

int HAL_FS_Read(HalFile_t f, void *buf, uint32_t size)
{
    if (!f) return -1;
    return (int)sAPI_fread(buf, 1, size, (SCFILE *)f);
}

int HAL_FS_Write(HalFile_t f, const void *buf, uint32_t size)
{
    if (!f) return -1;
    return (int)sAPI_fwrite(buf, 1, size, (SCFILE *)f);
}

int HAL_FS_Seek(HalFile_t f, int32_t offset, int whence)
{
    if (!f) return -1;
    return sAPI_fseek((SCFILE *)f, offset, whence);
}

int HAL_FS_Size(HalFile_t f)
{
    if (!f) return -1;
    sAPI_fseek((SCFILE *)f, 0, HAL_FS_SEEK_END);
    return (int)sAPI_ftell((SCFILE *)f);
}

int HAL_FS_Delete(const char *path)
{
    return sAPI_remove(path) == 0 ? 0 : -1;
}

/* ========================== HAL_FOTA ====================================== */

int HAL_FOTA_AppDownload(const char *url, uint32_t recvTimeoutMs)
{
    SCAppDownloadPram pram;

    if (!url) return -1;

    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        pram.mod = SC_APP_DOWNLOAD_HTTP_MOD;
    } else if (strncmp(url, "ftp://", 6) == 0 || strncmp(url, "ftps://", 7) == 0) {
        pram.mod = SC_APP_DOWNLOAD_FTP_MOD;
    } else {
        return -1;
    }
    pram.url = (char *)url;
    pram.recvtimeout = recvTimeoutMs;

    /* Theo demo SDK (demo_app_download.c): phai mo khoa bao ve flash truoc khi
     * tai, neu khong goi ghi xuong flash khong co hieu luc. */
    sAPI_FBF_Disable();

    return (int)sAPI_AppDownload(&pram);
}

int HAL_FOTA_AppPackageVerify(uint32_t *outSize)
{
    SCAppPackageInfo info = {0};
    int ret = sAPI_AppPackageCrc(&info);

    if (outSize) *outSize = (uint32_t)info.binSize;
    return ret;
}

int HAL_FOTA_SysStart(const char *url, HalFotaSysCb_t cb)
{
    /* static: SDK co the con tham chieu param sau khi ham tra ve */
    static struct SC_FotaApiParam param;
    const char *rest;
    const char *at;
    const char *colon;
    const char *slash;

    if (!url || !cb) return -1;
    memset(&param, 0, sizeof(param));

    if (strncmp(url, "http://", 7) == 0) {
        /* Theo demo SDK: HTTP truyen host khong kem scheme */
        param.mode = 1;
        rest = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        param.mode = 1;
        rest = url;
    } else if (strncmp(url, "ftp://", 6) == 0) {
        param.mode = 0;
        rest = url + 6;
        /* Tach user:pass@ (neu co) nam truoc dau '/' dau tien */
        slash = strchr(rest, '/');
        at = strchr(rest, '@');
        if (at && (!slash || at < slash)) {
            size_t ulen, plen = 0;
            colon = memchr(rest, ':', (size_t)(at - rest));
            ulen = (size_t)((colon ? colon : at) - rest);
            if (colon) plen = (size_t)(at - colon - 1);
            if (ulen >= sizeof(param.username) || plen >= sizeof(param.password)) return -1;
            memcpy(param.username, rest, ulen);
            if (colon) memcpy(param.password, colon + 1, plen);
            rest = at + 1;
        }
    } else {
        return -1;
    }

    if (rest[0] == '\0' || strlen(rest) >= sizeof(param.host)) return -1;
    strcpy(param.host, rest);
    /* SDK dung lenh AT+CFOTA=0,<mode>,"<host>",<user>,<pass>: de trong user/pass
     * thi AT parser bao "Missing AT command parameter", FOTA khong chay ma
     * sAPI_FotaServiceBegin van tra ve 0 → luon dien gia tri mac dinh. */
    if (param.username[0] == '\0') strcpy(param.username, "anonymous");
    if (param.password[0] == '\0') strcpy(param.password, "anonymous");
    param.sc_fota_cb = (sc_fota_callback)cb;

    return sAPI_FotaServiceBegin((void *)&param);
}
