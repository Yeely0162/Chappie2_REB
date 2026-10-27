/**
 * @file SyCommon.h
 * @brief Synology monitor: shared data model, configuration and module API.
 *
 * The app is split in two halves:
 *
 *   SyNet.cpp  - NVS configuration, HTTP/API session, JSON parsing and the
 *                single FreeRTOS worker task. It never touches LVGL.
 *   SyUi.cpp   - every LVGL object, gesture handling and the refresh timer.
 *                It never performs network I/O and never blocks.
 *
 * The only thing they share is the Snapshot below plus the Config, both
 * guarded by one mutex (DataLock/DataUnlock). Only the worker writes the
 * Snapshot, only the LVGL timer reads it.
 */
#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace Sy {

/* ------------------------------------------------------------------ pages */
enum PageId : uint8_t {
    PAGE_HOME = 0,
    PAGE_OVERVIEW,      /* swipe up on the home page: connected users */
    PAGE_SETTINGS,
    PAGE_STORAGE,
    PAGE_DISK,          /* one disk, opened from the storage list */
    PAGE_SYSTEM,
    PAGE_UPS,
    PAGE_MAX
};

/* ------------------------------------------------------------- connection */
enum ConnState : uint8_t {
    CONN_OFFLINE = 0,   /* no Wi-Fi / no configuration */
    CONN_NO_WIFI,
    CONN_CONNECTING,
    CONN_ONLINE,
    CONN_AUTH_FAIL,     /* login rejected, needs the user */
    CONN_ERROR          /* transport / server error */
};

/* --------------------------------------------------------------------- UPS */
enum UpsState : uint8_t {
    UPS_UNKNOWN = 0,
    UPS_NONE,           /* API answered, no UPS attached */
    UPS_LINE,           /* running from mains */
    UPS_BATTERY,        /* on battery */
    UPS_LOW_BATTERY,
    UPS_UNSUPPORTED,    /* API missing on this DSM */
    UPS_NO_PERMISSION,  /* API error 105 */
    UPS_ERROR
};

/* ------------------------------------------------------ connected users */
#define SY_MAX_SESSIONS 10

struct SessionInfo {
    char     who[20];       /* DSM account of the session */
    char     proto[12];     /* protocol string as reported, e.g. "HTTP/HTTPS" */
    char     from[16];      /* client address */
    uint32_t seconds;       /* online duration, 0xFFFFFFFF when unknown */
    bool     kickable;      /* NAS says the session may be terminated */
};

/* ------------------------------------------------------------------ disks */
#define SY_MAX_DISKS 8

struct DiskInfo {
    char     name[16];      /* "Drive 1" (hdd_info.diskno) */
    char     model[24];
    char     status[12];    /* hdd_info.status, e.g. "normal" */
    char     pool[16];      /* hdd_info.volume, e.g. "reuse_1" (pool id) */
    uint64_t capacity;
    int16_t  temp;          /* Celsius, -1 = unknown */
};

/* ---------------------------------------------------------------- volumes */
#define SY_MAX_VOLUMES 6

struct VolumeInfo {
    char     name[20];
    uint64_t total;
    uint64_t used;
    char     status[16];    /* "normal", "degraded", ... or "" when absent */
};

/* --------------------------------------------------------------- snapshot */
/*
 * Plain data only (no String): it is copied by value between tasks, so it
 * must stay trivially copyable and small.
 */
struct Snapshot {
    /* sequences: the UI redraws a page when its sequence changes */
    uint32_t fastSeq;
    uint32_t sysSeq;
    uint32_t storageSeq;
    uint32_t upsSeq;

    /* utilization (fast poll) */
    bool     cpuValid;
    bool     ramValid;
    int16_t  cpuUse;        /* 0..100 */
    int16_t  ramUse;        /* 0..100 */

    /* network throughput in bytes per second (fast poll) */
    bool     netValid;
    bool     netStale;      /* last sample is kept but no longer live */
    float    downBps;
    float    upBps;
    char     nic[12];

    /* interface link state (slow poll, SYNO.Core.System type=network) */
    bool     linkValid;
    bool     linkUp;
    int16_t  linkSpeed;     /* Mbps, -1 when the NAS does not report it */
    char     linkName[12];
    char     linkIp[20];

    /* system information (slow poll) */
    bool     sysValid;
    char     model[24];
    char     dsm[40];
    char     cpuSeries[36];
    int16_t  cores;
    float    ramMB;
    int16_t  sysTemp;
    bool     tempWarn;
    char     uptime[20];

    /* storage (slow poll) */
    bool     storageValid;
    bool     diskApiValid;   /* the storage response carried disk_info */
    uint8_t  diskCount;
    uint8_t  diskWarn;
    int16_t  diskTempMax;
    DiskInfo disks[SY_MAX_DISKS];
    uint8_t  volCount;
    VolumeInfo vols[SY_MAX_VOLUMES];

    /* UPS (slow poll) */
    UpsState upsState;
    bool     upsChargeValid;
    int16_t  upsCharge;      /* percent */
    bool     upsRuntimeValid;
    int32_t  upsRuntime;     /* seconds */
    char     upsModel[24];
    char     upsRaw[20];     /* raw status word when it is not understood */

    /* connected users (slow poll, SYNO.Core.CurrentConnection) */
    bool        usersValid;
    int16_t     usersErr;       /* last API error for the session list, 0 = ok */
    uint32_t    usersSeq;
    uint8_t     userCount;
    SessionInfo users[SY_MAX_SESSIONS];
    char        nasTime[24];     /* NAS wall clock used to derive the durations */

    /* connection / diagnostics */
    ConnState conn;
    int16_t   lastErr;       /* last DSM API error code, 0 = none */
    char      msg[64];       /* human readable, never contains credentials */
    uint32_t  lastFastMs;
    uint32_t  lastSlowMs;
    bool      hasData;

    /* session bookkeeping for the overview page */
    uint32_t  sessSince;     /* epoch (s) when this DSM login was accepted, 0 = unknown */
    uint32_t  sessSinceMs;   /* millis() at the same moment (always valid) */

    /* settings "Test connection" result, driven by the worker */
    uint8_t   testState;     /* 0 idle, 1 running, 2 ok, 3 fail */
    char      testMsg[64];
};

/* --------------------------------------------------------------- config */
struct Config {
    String   host;          /* IP address or host name, no scheme, no port */
    uint16_t port;
    String   user;
    String   pass;
    bool     https;

    bool configured() const { return host.length() > 0 && user.length() > 0; }
};

/* ------------------------------------------------------------ shared data */
bool      DataLock(uint32_t timeoutMs = 25);
void      DataUnlock();
Snapshot &Data();

/* ---------------------------------------------------------------- config */
void CfgGet(Config &out);
bool CfgSave(const Config &c);          /* validate, persist, apply */
void CfgClear();
bool CfgValidate(const Config &c, char *err, size_t errLen);

/* ------------------------------------------------------------ worker task */
void NetStart(void *bsp);
void NetStop();
void NetApplyConfig();                  /* re-read config, drop the session */
void NetTest(const Config &c);          /* test an unsaved draft */
void NetSetPage(uint8_t page);          /* poll-rate hint from the UI */
void NetRequestFast();                  /* refresh as soon as possible */

/* -------------------------------------------------------------- UI layer */
void UiBuild(void *bsp);
void UiDestroy();
void UiNavigate(PageId page, bool animate);
PageId UiCurrentPage();
void UiRefresh();                       /* called from the LVGL timer */
bool UiHandleBack();                    /* true when the B key was used */

} // namespace Sy
