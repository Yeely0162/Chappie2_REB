/**
 * @file SyNet.cpp
 * @brief Synology monitor: configuration store, DSM API session and worker.
 *
 * Everything in this file runs on the FreeRTOS worker task (except the small
 * configuration helpers, which are also called from the UI task). No LVGL
 * call is made here: results are published into Sy::Snapshot under a mutex
 * and the UI timer picks them up.
 *
 * API notes (verified against the reference script and DSM 7 documentation):
 *   SYNO.API.Auth                 login / logout
 *   SYNO.API.Info                 API discovery (path + highest version)
 *   SYNO.Core.System.Utilization  CPU / RAM percentages and NIC rx/tx
 *   SYNO.Core.System?type=info    model, DSM version, CPU, RAM, temp, uptime
 *   SYNO.Core.System?type=storage volumes (vol_info) and, when the DSM returns
 *                                 it, disk_info
 *   SYNO.Core.System?type=network interface link state and IP
 *   SYNO.Core.ExternalDevice.UPS  UPS state
 *   SYNO.Virtualization.Cluster   fallback CPU/RAM/NIC source (needs VMM)
 */
#include "SyCommon.h"

#include "../../../ChappieBsp/Chappie.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <string.h>

namespace Sy {

/* ------------------------------------------------------------- constants */
#define SY_NVS_NS       "syno"
#define SY_NVS_HOST     "host"
#define SY_NVS_PORT     "port"
#define SY_NVS_USER     "user"
#define SY_NVS_PASS     "pass"
#define SY_NVS_HTTPS    "https"

/* Request timing. Measured on the reference NAS (DSM 7.3): every API answers
 * in 85-250 ms except SYNO.Core.System.Utilization, which needs ~1.1-1.3 s.
 * The reply deadline therefore has to stay well above that, while a dead NAS
 * must not block the worker for long. */
#define SY_CONNECT_MS   4000
#define SY_REPLY_MS     6000
#define SY_MAX_BODY     32768

/* One request at a time: the worker is the only caller today, but a second
 * path (connection test) must never share a socket. */
static SemaphoreHandle_t s_httpMtx = nullptr;

/* Poll cadence. The task asked for a 2-3 s refresh of the data that is on
 * screen and a slower cadence for everything else. */
#define SY_FAST_ACTIVE_MS   3000    /* home page visible */
#define SY_FAST_IDLE_MS     10000   /* any other page */
#define SY_SLOW_ACTIVE_MS   20000   /* storage / system / ups visible */
#define SY_SLOW_HOME_MS     60000   /* everything else */

#define SY_RETRY_MIN_MS     5000
#define SY_RETRY_MAX_MS     60000

/* A DSM utilization rate is bytes/second and stays far below this on a
 * 1 GbE NAS. Anything larger is a cumulative counter, so it is diffed.
 *
 * If a DSM build reports cumulative byte counters that are still small right
 * after a NAS reboot, the auto detection cannot tell them apart from a rate.
 * Set SY_NET_FORCE_COUNTER to 1 to always difference rx/tx. */
#ifndef SY_NET_FORCE_COUNTER
#define SY_NET_FORCE_COUNTER 0
#endif
#define SY_COUNTER_HINT     (128.0 * 1024.0 * 1024.0)
/* Absolute sanity limit for a published rate (2 GB/s). */
#define SY_RATE_LIMIT       (2.0 * 1000.0 * 1000.0 * 1000.0)

/* Command bits handed from the UI to the worker. */
#define CMD_RELOGIN  0x01
#define CMD_TEST     0x02
#define CMD_FAST     0x04
#define CMD_SLOW     0x08

/* API result codes (DSM codes are positive and passed through). */
#define ERR_NET      (-1)   /* no TCP/TLS connection */
#define ERR_JSON     (-2)   /* body is JSON but malformed */
#define ERR_SHAPE    (-3)   /* JSON without the expected members */
#define ERR_HTTP     (-4)   /* non-200 HTTP status (500, 404, ...) */
#define ERR_NOTJSON  (-5)   /* 200 but an HTML/other page, not DSM JSON */

/* ------------------------------------------------------------------ state */
static CHAPPIE *s_dev = nullptr;

static SemaphoreHandle_t s_mtx = nullptr;
static Snapshot          s_data;            /* guarded by s_mtx */
static Config            s_cfg;             /* guarded by s_mtx */
static Config            s_testCfg;         /* guarded by s_mtx */

static portMUX_TYPE s_ctlMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_cmd = 0;
static volatile uint8_t  s_page = PAGE_HOME;
static volatile int      s_taskCount = 0;
static volatile uint32_t s_gen = 0;
static volatile uint32_t s_stopGen = 0;

/* Worker-only state (single writer). */
static struct {
    String   sid;
    bool     valid;
    uint32_t since;         /* epoch seconds when this session was accepted */
    uint32_t sinceMs;       /* millis() at the same moment */
} s_sess;
static int s_lastHttp = 0;      /* HTTP status of the last failed request */

/* One DSM API definition as advertised by SYNO.API.Info.
 *
 * minVer/maxVer come from the NAS; useVer is the version this build asks for
 * (the preferred version clamped into the advertised range). Never send a
 * version the NAS says it does not support. */
static struct ApiEntry {
    bool    present;
    char    path[20];
    uint8_t minVer;
    uint8_t maxVer;
    uint8_t useVer;
} s_apiUsers = { true,  "entry.cgi", 1, 1, 1 },
  s_apiAuth  = { true,  "auth.cgi", 1, 7, 3 },
  s_apiSys   = { true,  "entry.cgi", 1, 3, 3 },
  s_apiUtil  = { true,  "entry.cgi", 1, 1, 1 },
  s_apiUps   = { true,  "entry.cgi", 1, 1, 1 },
  s_apiVmm   = { true,  "entry.cgi", 1, 2, 1 };

/* Network rate derivation. */
static struct {
    bool     have;
    bool     counter;       /* source reports cumulative bytes */
    double   prevRx;
    double   prevTx;
    uint32_t prevMs;
} s_rate = { false, false, 0, 0, 0 };

/* --------------------------------------------------------------- helpers */
static void setStr(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t i = 0;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* Same as setStr() but drops leading/trailing whitespace. DSM sends strings
 * such as "Back-UPS BK650M2-CH\n" with a trailing newline. */
static void setStrTrim(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0) return;
    if (!src) { dst[0] = 0; return; }
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
    size_t len = strlen(src);
    while (len > 0) {
        char c = src[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') len--;
        else break;
    }
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Reset one API entry to a known-good default. A braced assignment such as
 * `s_apiSys = { true, "entry.cgi", 3 };` is not valid C++ for an aggregate
 * that is already constructed, so the fields are set explicitly here. */
static void apiEntryReset(ApiEntry &e, uint8_t preferred)
{
    e.present = true;
    setStr(e.path, sizeof(e.path), "entry.cgi");
    e.minVer = 1;
    e.maxVer = preferred;
    e.useVer = preferred;
}

static String urlEncode(const String &in)
{
    static const char *hex = "0123456789ABCDEF";
    String out;
    out.reserve(in.length() * 3 + 8);
    for (size_t i = 0; i < in.length(); i++) {
        uint8_t c = (uint8_t)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

/* Extract an unsigned 64 bit number that may be sent as string or number. */
static uint64_t jsonU64(JsonVariant v)
{
    if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        return s ? strtoull(s, nullptr, 10) : 0;
    }
    if (v.is<double>()) {
        double d = v.as<double>();
        return d > 0 ? (uint64_t)d : 0;
    }
    return 0;
}

/* Read an integer only when the JSON value really is numeric. Missing or
 * non-numeric values keep `ok` false so the UI can show "n/a" instead of a
 * made-up 0 (a missing UPS charge must not read as 0 %). */
static int jsonInt(JsonVariant v, bool &ok)
{
    ok = false;
    if (v.isNull()) return 0;
    if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        if (!s || !s[0]) return 0;
        char *end = nullptr;
        long n = strtol(s, &end, 10);
        if (end == s) return 0;
        ok = true;
        return (int)n;
    }
    if (v.is<double>()) {
        ok = true;
        return (int)v.as<double>();
    }
    return 0;
}

static double jsonDouble(JsonVariant v, bool &ok)
{
    ok = false;
    if (v.isNull()) return 0.0;
    if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        if (!s || !s[0]) return 0.0;
        char *end = nullptr;
        double d = strtod(s, &end);
        if (end == s) return 0.0;
        ok = true;
        return d;
    }
    if (v.is<double>()) {
        ok = true;
        return v.as<double>();
    }
    return 0.0;
}

static bool jsonBool(JsonVariant v)
{
    if (v.is<bool>()) return v.as<bool>();
    if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        if (!s || !s[0]) return false;
        char c = (char)tolower((unsigned char)s[0]);
        return c == 't' || c == '1';
    }
    if (v.is<double>()) return v.as<double>() != 0.0;
    return false;
}

static const char *apiErrText(int code)
{
    switch (code) {
    case ERR_NET:   return "NAS unreachable";
    case ERR_JSON:  return "Invalid JSON from NAS";
    case ERR_SHAPE: return "Unexpected JSON layout";
    case ERR_HTTP:  return "HTTP error from NAS";
    case ERR_NOTJSON: return "Non-JSON reply from NAS";
    case 101: return "Invalid API parameters";
    case 102: return "API not available on this DSM";
    case 103: return "API method not available";
    case 104: return "API version not supported";
    case 105: return "Insufficient permission";
    case 106: return "Connection timed out";
    case 107: return "Multiple logins detected";
    case 110:
    case 111:
    case 117:
    case 118: return "NAS busy";
    case 119: return "Session expired";
    case 400: return "Invalid password";
    case 401: return "Guest or disabled account";
    case 402: return "Permission denied";
    case 403: return "OTP code required";
    case 404: return "OTP code incorrect";
    case 406: return "OTP enforced for this account";
    case 407: return "Too many attempts, account blocked";
    case 409:
    case 410: return "Password must be changed";
    case 411: return "Account locked";
    default:  return "DSM error";
    }
}

static bool authError(int code)
{
    return code == 400 || code == 401 || code == 402 || code == 403 ||
           code == 404 || code == 406 || code == 407 || code == 409 ||
           code == 410 || code == 411 || code == 105;
}

static void fmtErr(char *buf, size_t n, int code)
{
    if (code == ERR_HTTP)
        snprintf(buf, n, "HTTP %d from NAS", s_lastHttp);
    else if (code == ERR_NOTJSON)
        snprintf(buf, n, "Non-JSON reply (HTTP %d)", s_lastHttp);
    else
        snprintf(buf, n, "%s (%d)", apiErrText(code), code);
}

/* ------------------------------------------------------------ data access */
bool DataLock(uint32_t timeoutMs)
{
    if (!s_mtx) return false;
    return xSemaphoreTake(s_mtx, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void DataUnlock()
{
    if (s_mtx) xSemaphoreGive(s_mtx);
}

Snapshot &Data()
{
    return s_data;
}
/* ------------------------------------------------------------ task count */
static void taskCountAdd(int delta)
{
    portENTER_CRITICAL(&s_ctlMux);
    s_taskCount += delta;
    portEXIT_CRITICAL(&s_ctlMux);
}

static int taskCountGet()
{
    portENTER_CRITICAL(&s_ctlMux);
    int v = s_taskCount;
    portEXIT_CRITICAL(&s_ctlMux);
    return v;
}

static uint32_t takeCommands()
{
    portENTER_CRITICAL(&s_ctlMux);
    uint32_t v = s_cmd;
    s_cmd = 0;
    portEXIT_CRITICAL(&s_ctlMux);
    return v;
}

static void postCommand(uint32_t bits)
{
    portENTER_CRITICAL(&s_ctlMux);
    s_cmd |= bits;
    portEXIT_CRITICAL(&s_ctlMux);
}

/* --------------------------------------------------------------- config */
static void cfgDefaults(Config &c)
{
    c.host = "";
    c.port = 5000;
    c.user = "";
    c.pass = "";
    c.https = true;
}

void CfgGet(Config &out)
{
    if (!DataLock(100)) {
        cfgDefaults(out);
        return;
    }
    out = s_cfg;
    DataUnlock();
}

bool CfgValidate(const Config &c, char *err, size_t errLen)
{
    if (err && errLen) err[0] = 0;
    if (c.host.length() == 0) {
        if (err) snprintf(err, errLen, "Server address is empty");
        return false;
    }
    if (c.host.length() > 63) {
        if (err) snprintf(err, errLen, "Server address is too long");
        return false;
    }
    if (c.user.length() == 0) {
        if (err) snprintf(err, errLen, "Account is empty");
        return false;
    }
    if (c.user.length() > 63) {
        if (err) snprintf(err, errLen, "Account name is too long");
        return false;
    }
    if (c.pass.length() > 128) {
        if (err) snprintf(err, errLen, "Password is too long");
        return false;
    }
    if (c.port == 0) {
        if (err) snprintf(err, errLen, "Port must be 1-65535");
        return false;
    }
    return true;
}

/* Accept "192.168.1.2", "nas.local", "nas:5001" or a pasted URL and split it
 * into host/port/protocol. IPv6 literals are not supported. */
static void cfgSanitize(Config &c)
{
    String h = c.host;
    h.trim();

    String lower = h;
    lower.toLowerCase();
    if (lower.startsWith("https://")) {
        c.https = true;
        h = h.substring(8);
    } else if (lower.startsWith("http://")) {
        c.https = false;
        h = h.substring(7);
    }

    int slash = h.indexOf('/');
    if (slash >= 0) h = h.substring(0, slash);

    int colon = h.lastIndexOf(':');
    if (colon > 0 && colon < (int)h.length() - 1) {
        long p = h.substring(colon + 1).toInt();
        if (p > 0 && p < 65536) c.port = (uint16_t)p;
        h = h.substring(0, colon);
    }

    h.trim();
    c.host = h;

    if (c.port == 0) c.port = c.https ? 5001 : 5000;
}

void CfgLoad()
{
    Config c;
    cfgDefaults(c);

    /* First boot has no keys; Preferences::getString() logs an error for a
     * missing key even when a default is supplied, so check isKey() first and
     * leave everything empty instead of asking for a login with "" host. */
    Preferences p;
    if (p.begin(SY_NVS_NS, true)) {
        if (p.isKey(SY_NVS_HOST))  c.host  = p.getString(SY_NVS_HOST, "");
        if (p.isKey(SY_NVS_USER))  c.user  = p.getString(SY_NVS_USER, "");
        if (p.isKey(SY_NVS_PASS))  c.pass  = p.getString(SY_NVS_PASS, "");
        if (p.isKey(SY_NVS_PORT))  c.port  = p.getUShort(SY_NVS_PORT, 5000);
        if (p.isKey(SY_NVS_HTTPS)) c.https = p.getBool(SY_NVS_HTTPS, false);
        p.end();
    }
    c.host.trim();
    c.user.trim();
    if (c.port == 0) c.port = c.https ? 5001 : 5000;

    if (DataLock(200)) {
        s_cfg = c;
        DataUnlock();
    }
}

bool CfgSave(const Config &in)
{
    Config c = in;
    cfgSanitize(c);
    c.user.trim();

    char err[48];
    if (!CfgValidate(c, err, sizeof(err))) {
        printf("[Syno] config rejected: %s\n", err);
        return false;
    }

    Preferences p;
    if (!p.begin(SY_NVS_NS, false)) {
        printf("[Syno] NVS open failed\n");
        return false;
    }
    p.putString(SY_NVS_HOST, c.host);
    p.putString(SY_NVS_USER, c.user);
    p.putString(SY_NVS_PASS, c.pass);
    p.putUShort(SY_NVS_PORT, c.port);
    p.putBool(SY_NVS_HTTPS, c.https);
    p.end();

    if (DataLock(200)) {
        s_cfg = c;
        DataUnlock();
    }

    /* Never print the account or the password. */
    printf("[Syno] config saved (%s:%u, %s)\n", c.host.c_str(), (unsigned)c.port,
           c.https ? "https" : "http");
    return true;
}

void CfgClear()
{
    Preferences p;
    if (p.begin(SY_NVS_NS, false)) {
        p.clear();
        p.end();
    }
    Config c;
    cfgDefaults(c);
    if (DataLock(200)) {
        s_cfg = c;
        DataUnlock();
    }
    printf("[Syno] config cleared\n");
}

/* ----------------------------------------------------------------- HTTP */
/* ------------------------------------------------------------- sockets
 *
 * HTTPClient is deliberately not used here. On this NAS nginx answers with
 * "Connection: close", and HTTPClient's teardown then hits
 *   [E][WiFiClient.cpp] flush(): fail on fd n, errno 128 "Socket is not connected"
 * while a slow request raises "error(-11): read Timeout" from inside its
 * header parser. Both come from HTTPClient's own socket handling, not from
 * the data. This implementation owns the socket: one deadline for the whole
 * reply, no keep-alive, no flush(), stop() only.
 */

/* Non-blocking wait for data with a deadline (millis based, wrap safe). */
static bool sockWait(WiFiClient *cli, uint32_t deadline)
{
    while ((int32_t)(millis() - deadline) < 0) {
        if (cli->available() > 0) return true;
        if (!cli->connected()) return cli->available() > 0;
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    return cli->available() > 0;
}

static bool readLine(WiFiClient *cli, String &line, uint32_t deadline)
{
    line = "";
    while (sockWait(cli, deadline)) {
        int c = cli->read();
        if (c < 0) continue;
        if (c == '\n') {
            int n = (int)line.length();
            if (n > 0 && line[n - 1] == '\r') line.remove(n - 1);
            return true;
        }
        if (line.length() < 400) line += (char)c;
    }
    return false;
}

static bool readExact(WiFiClient *cli, uint32_t len, String &out, uint32_t deadline)
{
    while (len > 0) {
        if (out.length() >= SY_MAX_BODY) return false;
        if (!sockWait(cli, deadline)) return false;

        uint8_t buf[256];
        size_t want = len < sizeof(buf) ? len : sizeof(buf);
        int n = cli->read(buf, want);
        if (n <= 0) continue;

        out.concat((const char *)buf, (unsigned)n);
        len -= (uint32_t)n;
    }
    return true;
}

static bool readChunked(WiFiClient *cli, String &out, uint32_t deadline)
{
    for (;;) {
        String line;
        if (!readLine(cli, line, deadline)) return false;

        long size = strtol(line.c_str(), nullptr, 16);
        if (size <= 0) {
            /* last chunk: swallow the trailer headers */
            while (readLine(cli, line, deadline) && line.length() > 0) { }
            return true;
        }
        if (!readExact(cli, (uint32_t)size, out, deadline)) return false;
        readLine(cli, line, deadline);      /* CRLF that ends the chunk */
    }
}

static bool readUntilClose(WiFiClient *cli, String &out, uint32_t deadline)
{
    for (;;) {
        if (out.length() >= SY_MAX_BODY) return false;

        int c = cli->read();
        if (c >= 0) {
            out += (char)c;
            continue;
        }
        if (!cli->connected()) return true;
        if ((int32_t)(millis() - deadline) >= 0) return false;
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

struct HttpGuard {
    bool held;
    HttpGuard()
    {
        held = (s_httpMtx &&
                xSemaphoreTake(s_httpMtx, pdMS_TO_TICKS(2500)) == pdTRUE);
    }
    ~HttpGuard()
    {
        if (held) xSemaphoreGive(s_httpMtx);
    }
};

static bool httpFetch(const String &pathQuery, const String &postBody, String &body, int &httpCode)
{
    body = "";
    httpCode = 0;

    Config c;
    CfgGet(c);
    if (!c.configured()) return false;

    HttpGuard guard;
    if (!guard.held) return false;

    WiFiClient plain;
    WiFiClientSecure tls;
    WiFiClient *cli = &plain;
    bool connected = false;

    /* connect() is called on the concrete object: the timeout overload is not
     * part of the Client interface, so a call through the base pointer could
     * pick the plain-TCP implementation for an https:// URL. Reads and writes
     * below go through the base pointer, which is virtual and dispatches
     * correctly. */
    if (c.https) {
        /* The board cannot store a CA for a private NAS certificate. TLS still
         * protects the credentials on the wire, but the peer is not
         * authenticated; the UI says so explicitly. */
        tls.setInsecure();
        connected = (tls.connect(c.host.c_str(), c.port, SY_CONNECT_MS) == 1);
        cli = &tls;
    } else {
        connected = (plain.connect(c.host.c_str(), c.port, SY_CONNECT_MS) == 1);
    }

    if (!connected) {
        cli->stop();
        return false;               /* refused / no route / handshake failed */
    }

    const bool post = postBody.length() > 0;
    String req;
    req.reserve(256 + postBody.length());
    req += post ? "POST " : "GET ";
    req += pathQuery;
    req += " HTTP/1.1\r\nHost: ";
    req += c.host;
    req += ':';
    req += String((unsigned)c.port);
    req += "\r\nUser-Agent: Chappie2-Synology\r\n";
    req += "Accept: application/json\r\n";
    req += "Connection: close\r\n";        /* nginx closes anyway */
    if (post) {
        req += "Content-Type: application/x-www-form-urlencoded\r\n";
        req += "Content-Length: ";
        req += String((unsigned)postBody.length());
        req += "\r\n";
    }
    req += "\r\n";
    if (post) req += postBody;

    size_t sent = cli->write((const uint8_t *)req.c_str(), req.length());
    if (sent != req.length()) {
        cli->stop();
        return false;
    }

    uint32_t deadline = millis() + SY_REPLY_MS;

    String status;
    if (!readLine(cli, status, deadline)) {
        cli->stop();
        return false;               /* no answer inside the deadline */
    }
    if (status.startsWith("HTTP/")) {
        int sp = status.indexOf(' ');
        if (sp > 0) httpCode = status.substring(sp + 1).toInt();
    }
    if (httpCode <= 0) {
        cli->stop();
        return false;
    }

    long contentLen = -1;
    bool chunked = false;
    for (int i = 0; i < 40; i++) {
        String h;
        if (!readLine(cli, h, deadline)) {
            cli->stop();
            return false;
        }
        if (h.length() == 0) break;

        String hl = h;
        hl.toLowerCase();
        if (hl.startsWith("content-length:")) {
            contentLen = hl.substring(15).toInt();
        } else if (hl.startsWith("transfer-encoding:") && hl.indexOf("chunked") >= 0) {
            chunked = true;
        }
    }

    bool ok = false;
    if (httpCode == 200) {
        if (chunked) ok = readChunked(cli, body, deadline);
        else if (contentLen >= 0) ok = readExact(cli, (uint32_t)contentLen, body, deadline);
        else ok = readUntilClose(cli, body, deadline);
    }

    cli->stop();
    return ok && httpCode == 200;
}

/* 0 on success, positive DSM error code, negative transport/parse error. */
static int apiCall(const String &pathQuery, const String &postBody, DynamicJsonDocument &doc)
{
    int httpCode = 0;

    for (int attempt = 0; attempt < 2; attempt++) {
        uint32_t t0 = millis();
        String body;
        if (!httpFetch(pathQuery, postBody, body, httpCode)) {
            /* A real HTTP error is reported as such; anything else (no route,
             * TLS failure, timeout) is a transport error. */
            if (httpCode >= 400) {
                s_lastHttp = httpCode;
                return ERR_HTTP;
            }
            /* One retry on a fresh socket, but only for a quick failure: a
             * full timeout would just double the dead-NAS delay. */
            if (attempt == 0 && (millis() - t0) < 3000) {
                vTaskDelay(pdMS_TO_TICKS(120));
                continue;
            }
            return ERR_NET;
        }

        /* DSM answers with a JSON object on this CGI path. Anything else (an
         * HTML portal, a proxy error page) is not parsed as JSON. */
        const char *p = body.c_str();
        while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;
        if (*p != '{') {
            s_lastHttp = httpCode;
            return ERR_NOTJSON;
        }

        DeserializationError err = deserializeJson(doc, p);
        if (err) return ERR_JSON;

        if (!doc["success"].as<bool>()) {
            int code = doc["error"]["code"] | 0;
            return code > 0 ? code : ERR_SHAPE;
        }
        return 0;
    }
    return ERR_NET;
}

static String apiPath(const ApiEntry &e)
{
    return String("/webapi/") + e.path;
}

static String withSid(String query)
{
    query += "&_sid=";
    query += urlEncode(s_sess.sid);
    return query;
}

/* One line per failing API, at most every 30 s for the same code: enough to
 * tell from the serial log which request went wrong without flooding. */
static void logApiError(const char *what, int rc)
{
    static uint32_t lastMs = 0;
    static int lastRc = 0;
    static const char *lastWhat = "";

    uint32_t now = millis();
    if (rc == lastRc && what == lastWhat && (now - lastMs) < 30000) return;
    lastMs = now;
    lastRc = rc;
    lastWhat = what;
    printf("[Syno] %s failed rc=%d\n", what, rc);
}

/* -------------------------------------------------------------- publish */
static void publishConn(ConnState st, const char *msg)
{
    if (!DataLock(50)) return;
    s_data.conn = st;
    if (msg) setStr(s_data.msg, sizeof(s_data.msg), msg);
    if (st == CONN_ONLINE) {
        s_data.sessSince = s_sess.since;
        s_data.sessSinceMs = s_sess.sinceMs;
    }

    /* Keep the last measured rate (it is still a real number) but mark it as
     * no longer live, so the UI can grey it out instead of pretending the NAS
     * is still reporting. */
    if (st != CONN_ONLINE && st != CONN_CONNECTING) {
        if (s_data.netValid) s_data.netStale = true;
    }
    DataUnlock();

    /* The differential baseline is meaningless across a gap. */
    if (st != CONN_ONLINE) s_rate.have = false;
}

static void publishFastError(int code)
{
    char m[64];
    fmtErr(m, sizeof(m), code);
    if (DataLock(50)) {
        s_data.lastErr = (int16_t)(code > 0 ? code : 0);
        s_data.netValid = false;
        setStr(s_data.msg, sizeof(s_data.msg), m);
        DataUnlock();
    }
}

/* Derive a byte/second rate from the NIC counters (or pass a rate through).
 * Handles counter resets, reconnects and the "too soon" case. */
static void publishNetwork(double rawRx, double rawTx, const char *nicName, uint32_t nowMs)
{
    double down = 0.0;
    double up = 0.0;

    if (!s_rate.have) {
        s_rate.have = true;
        s_rate.prevRx = rawRx;
        s_rate.prevTx = rawTx;
        s_rate.prevMs = nowMs;
        /* A value larger than any plausible link rate must be a counter. */
        if (rawRx > SY_COUNTER_HINT || rawTx > SY_COUNTER_HINT) s_rate.counter = true;
#if SY_NET_FORCE_COUNTER
        s_rate.counter = true;
#endif
    } else if (s_rate.counter) {
        uint32_t dt = nowMs - s_rate.prevMs;
        if (rawRx < s_rate.prevRx || rawTx < s_rate.prevTx) {
            /* Counter reset (NAS reboot, interface reset, API restart). */
            s_rate.prevRx = rawRx;
            s_rate.prevTx = rawTx;
            s_rate.prevMs = nowMs;
        } else if (dt >= 200) {
            down = (rawRx - s_rate.prevRx) * 1000.0 / (double)dt;
            up   = (rawTx - s_rate.prevTx) * 1000.0 / (double)dt;
            s_rate.prevRx = rawRx;
            s_rate.prevTx = rawTx;
            s_rate.prevMs = nowMs;
        } else {
            return;     /* sampled again too quickly, keep the previous value */
        }
    } else {
        /* The API already reports bytes/second: use it as-is. */
        down = rawRx;
        up = rawTx;
        s_rate.prevRx = rawRx;
        s_rate.prevTx = rawTx;
        s_rate.prevMs = nowMs;
    }

    if (!(down >= 0.0) || down > SY_RATE_LIMIT) down = 0.0;
    if (!(up >= 0.0) || up > SY_RATE_LIMIT) up = 0.0;

    if (DataLock(50)) {
        s_data.downBps = (float)down;
        s_data.upBps = (float)up;
        s_data.netValid = true;
        s_data.netStale = false;
        if (nicName) setStr(s_data.nic, sizeof(s_data.nic), nicName);
        s_data.fastSeq++;
        s_data.lastFastMs = millis();
        s_data.hasData = true;
        DataUnlock();
    }
}

static void publishCpuRam(int cpu, int ram)
{
    if (!DataLock(50)) return;
    if (cpu >= 0) {
        s_data.cpuUse = (int16_t)clampInt(cpu, 0, 100);
        s_data.cpuValid = true;
    }
    if (ram >= 0) {
        s_data.ramUse = (int16_t)clampInt(ram, 0, 100);
        s_data.ramValid = true;
    }
    if (cpu >= 0 || ram >= 0) s_data.hasData = true;
    DataUnlock();
}

/* -------------------------------------------------------------- parsers */
static int fetchUtilization()
{
    if (!s_apiUtil.present) return 102;

    String q = apiPath(s_apiUtil);
    q += "?api=SYNO.Core.System.Utilization&version=";
    q += String((unsigned)s_apiUtil.useVer);
    q += "&method=get";
    q = withSid(q);

    DynamicJsonDocument doc(6144);
    int rc = apiCall(q, "", doc);
    if (rc) return rc;

    JsonObject d = doc["data"].as<JsonObject>();
    if (d.isNull()) return ERR_SHAPE;

    /* Calibration (verified against this NAS, DSM 7.3, DS718+ / 4 cores):
     * user_load, system_load and other_load are percentages of the whole CPU
     * and are additive; user+system+other tracked the independent
     * Virtualization.Cluster "cpu_usage" within a few points on every sample
     * (27 vs 25, 31 vs 29, ...). The DSM web UI polls on its own schedule, so
     * comparing the two screens at different instants always shows a small
     * difference - no offset is applied here on purpose. */
    int cpu = -1;
    JsonObject cpuObj = d["cpu"].as<JsonObject>();
    if (!cpuObj.isNull()) {
        bool uOk = false, sOk = false, oOk = false;
        int u = jsonInt(cpuObj["user_load"], uOk);
        int sy = jsonInt(cpuObj["system_load"], sOk);
        int ot = jsonInt(cpuObj["other_load"], oOk);
        if (uOk && sOk) cpu = u + sy + (oOk && ot > 0 ? ot : 0);
    }

    /* real_usage is the used-memory percentage (58-59 on this NAS, matching
     * VMM's ram_usage). It is used as-is, never multiplied or inverted. */
    bool ramOk = false;
    int ram = jsonInt(d["memory"]["real_usage"], ramOk);
    if (!ramOk) ram = -1;

    /* DSM sends one "total" row plus one row per interface, and the total row
     * already contains the sum (verified on DSM 7.3: total == eth0). Adding
     * both would double every rate, so the total row wins when it exists. */
    double rx = 0.0, tx = 0.0, totalRx = 0.0, totalTx = 0.0;
    double bestTraffic = -1.0;
    bool haveTotal = false;
    int nics = 0;
    char nic[12] = { 0 };

    for (JsonVariant v : d["network"].as<JsonArray>()) {
        const char *dev = v["device"] | "";
        bool rOk = false, tOk = false;
        double r = jsonDouble(v["rx"], rOk);
        double t = jsonDouble(v["tx"], tOk);

        if (strcmp(dev, "total") == 0) {
            haveTotal = true;
            totalRx = (rOk && r > 0) ? r : 0.0;
            totalTx = (tOk && t > 0) ? t : 0.0;
            continue;
        }

        /* Label the card with the busiest interface, not just the first one:
         * an idle eth1 is often listed before the active one. */
        double traffic = (rOk ? r : 0.0) + (tOk ? t : 0.0);
        if (dev[0] && traffic > bestTraffic) {
            bestTraffic = traffic;
            setStr(nic, sizeof(nic), dev);
        }

        if (rOk && r > 0) rx += r;
        if (tOk && t > 0) tx += t;
        nics++;
    }

    if (haveTotal) {
        rx = totalRx;
        tx = totalTx;
        nics = 1;
    }

    publishCpuRam(cpu, ram);
    if (nics > 0) {
        publishNetwork(rx, tx, nic, millis());
    } else if (DataLock(50)) {
        /* Interface disappeared: do not keep showing the last rate. */
        s_data.netValid = false;
        s_data.downBps = 0;
        s_data.upBps = 0;
        s_rate.have = false;
        s_data.fastSeq++;
        DataUnlock();
    }
    return 0;
}

/* Fallback source: Virtualization Manager host statistics. Some NAS only
 * expose CPU/RAM/NIC numbers through this API. */
static int fetchVmm()
{
    if (!s_apiVmm.present) return 102;

    String q = apiPath(s_apiVmm);
    q += "?api=SYNO.Virtualization.Cluster&method=get_host&version=";
    q += String((unsigned)s_apiVmm.useVer);
    q = withSid(q);

    DynamicJsonDocument doc(6144);
    int rc = apiCall(q, "", doc);
    if (rc) return rc;

    JsonObject host = doc["data"]["hosts"][0].as<JsonObject>();
    if (host.isNull()) return ERR_SHAPE;

    bool cpuOk = false, ramOk = false;
    int cpu = jsonInt(host["cpu_usage"], cpuOk);
    int ram = jsonInt(host["ram_usage"], ramOk);
    if (!cpuOk) cpu = -1;
    if (!ramOk) ram = -1;

    double rx = 0.0, tx = 0.0;
    int nics = 0;
    char nic[12] = { 0 };
    for (JsonVariant v : host["nics"].as<JsonArray>()) {
        const char *status = v["status"] | "";
        if (status[0] && strcmp(status, "connected") != 0) continue;
        if (nics == 0) {
            const char *dev = v["device"] | "";
            setStr(nic, sizeof(nic), dev);
        }
        bool rOk = false, tOk = false;
        double r = jsonDouble(v["rx"], rOk);
        double t = jsonDouble(v["tx"], tOk);
        if (rOk && r > 0) rx += r;
        if (tOk && t > 0) tx += t;
        nics++;
    }

    publishCpuRam(cpu, ram);
    if (nics > 0) publishNetwork(rx, tx, nic, millis());
    return 0;
}

static int fetchFast()
{
    int rc = fetchUtilization();
    if (rc == 0) return 0;

    if (rc == 102 || rc == 103 || rc == 104 || rc == 101) {
        printf("[Syno] utilization unavailable (%d), trying VMM\n", rc);
        s_apiUtil.present = false;
        rc = fetchVmm();
        if (rc == 0) return 0;
    }
    return rc;
}

static int fetchSystemInfo()
{
    String q = apiPath(s_apiSys);
    q += "?api=SYNO.Core.System&method=info&version=";
    q += String((unsigned)s_apiSys.useVer);
    q = withSid(q);

    DynamicJsonDocument doc(4096);
    int rc = apiCall(q, "", doc);
    if (rc) return rc;

    JsonObject d = doc["data"].as<JsonObject>();
    if (d.isNull()) return ERR_SHAPE;

    char model[24] = { 0 };
    char dsm[40] = { 0 };
    char cpu[36] = { 0 };
    char uptime[20] = { 0 };
    setStrTrim(model, sizeof(model), d["model"] | "");
    setStrTrim(dsm, sizeof(dsm), d["firmware_ver"] | "");
    setStrTrim(uptime, sizeof(uptime), d["up_time"] | "");

    /* DSM 7.3 reports cpu_series="J3455" and cpu_vendor="INTEL" separately. */
    const char *series = d["cpu_series"] | "";
    const char *vendor = d["cpu_vendor"] | "";
    if (vendor[0] && series[0]) snprintf(cpu, sizeof(cpu), "%s %s", vendor, series);
    else setStr(cpu, sizeof(cpu), series[0] ? series : vendor);

    bool coresOk = false;
    int cores = jsonInt(d["cpu_cores"], coresOk);
    if (!coresOk) cores = 0;

    bool ramMbOk = false;
    double ramMB = jsonDouble(d["ram_size"], ramMbOk);
    if (!ramMbOk) ramMB = 0.0;

    bool tempOk = false;
    int temp = jsonInt(d["sys_temp"], tempOk);
    if (!tempOk) temp = -1;

    bool warn = jsonBool(d["temperature_warning"]);

    if (DataLock(80)) {
        setStr(s_data.model, sizeof(s_data.model), model);
        setStr(s_data.dsm, sizeof(s_data.dsm), dsm);
        setStr(s_data.cpuSeries, sizeof(s_data.cpuSeries), cpu);
        setStr(s_data.uptime, sizeof(s_data.uptime), uptime);
        s_data.cores = (int16_t)clampInt(cores, 0, 256);
        s_data.ramMB = (float)ramMB;
        s_data.sysTemp = (int16_t)clampInt(temp, -1, 200);
        s_data.tempWarn = warn;
        s_data.sysValid = true;
        s_data.sysSeq++;
        s_data.lastSlowMs = millis();
        DataUnlock();
    }
    return 0;
}

static int fetchNetworkInfo()
{
    String q = apiPath(s_apiSys);
    q += "?api=SYNO.Core.System&type=network&method=info&version=";
    q += String((unsigned)s_apiSys.useVer);
    q = withSid(q);

    DynamicJsonDocument doc(6144);
    int rc = apiCall(q, "", doc);
    if (rc) return rc;

    JsonObject d = doc["data"].as<JsonObject>();
    if (d.isNull()) return ERR_SHAPE;

    /* DSM 7.3 returns the interface list as "nif" with id/addr/status/speed;
     * older builds used interfaces[].ifname. Both shapes are accepted. */
    JsonArray ifs = d["nif"].as<JsonArray>();
    if (ifs.isNull()) ifs = d["interfaces"].as<JsonArray>();
    if (ifs.isNull()) ifs = d["ifaces"].as<JsonArray>();

    char name[12] = { 0 };
    char ip[20] = { 0 };
    int16_t speed = -1;
    bool up = false;
    bool found = false;

    for (JsonVariant v : ifs) {
        const char *ifname = v["id"] | (v["ifname"] | (v["name"] | ""));
        const char *addr = v["addr"] | (v["ip"] | "");
        const char *st = v["status"] | "";
        if (!ifname[0]) continue;

        bool isUp;
        if (st[0]) {
            isUp = (strcmp(st, "connected") == 0) || (strcmp(st, "up") == 0) ||
                   (strcmp(st, "normal") == 0);
        } else {
            isUp = addr[0] && strcmp(addr, "0.0.0.0") != 0;
        }

        if (!found || (isUp && !up)) {
            bool okSpeed = false;
            int sp = jsonInt(v["speed"], okSpeed);
            setStr(name, sizeof(name), ifname);
            setStr(ip, sizeof(ip), addr);
            speed = (okSpeed && sp > 0) ? (int16_t)clampInt(sp, 0, 100000) : -1;
            up = isUp;
            found = true;
        }
        if (isUp) break;
    }

    if (DataLock(80)) {
        s_data.linkValid = found;
        s_data.linkUp = up;
        s_data.linkSpeed = speed;
        setStr(s_data.linkName, sizeof(s_data.linkName), name);
        setStr(s_data.linkIp, sizeof(s_data.linkIp), ip);
        DataUnlock();
    }
    return 0;
}

static int fetchStorage()
{
    String q = apiPath(s_apiSys);
    q += "?api=SYNO.Core.System&type=storage&method=info&version=";
    q += String((unsigned)s_apiSys.useVer);
    q = withSid(q);

    DynamicJsonDocument doc(12288);
    int rc = apiCall(q, "", doc);
    if (rc) return rc;

    JsonObject d = doc["data"].as<JsonObject>();
    if (d.isNull()) return ERR_SHAPE;

    VolumeInfo vols[SY_MAX_VOLUMES];
    memset(vols, 0, sizeof(vols));
    uint8_t n = 0;

    for (JsonObject v : d["vol_info"].as<JsonArray>()) {
        if (n >= SY_MAX_VOLUMES) break;
        setStr(vols[n].name, sizeof(vols[n].name), v["name"] | "");
        vols[n].total = jsonU64(v["total_size"]);
        vols[n].used  = jsonU64(v["used_size"]);
        if (vols[n].used > vols[n].total) vols[n].used = vols[n].total;
        setStr(vols[n].status, sizeof(vols[n].status), v["status"] | "");
        n++;
    }

    /* Disk detail is only reported by some DSM builds; never invent it.
     * DSM 7.3 (verified) returns data.hdd_info[]:
     *   diskno "Drive 1", model, status/overview_status, temp, capacity (string),
     *   diskType, diskPath, volume "reuse_1" (a storage POOL id, not a volume). */
    bool diskApi = false;
    uint8_t diskCount = 0;
    uint8_t diskWarn = 0;
    int16_t diskTemp = -1;
    DiskInfo dlist[SY_MAX_DISKS];
    memset(dlist, 0, sizeof(dlist));

    JsonArray disks = d["hdd_info"].as<JsonArray>();
    if (disks.isNull()) disks = d["disk_info"].as<JsonArray>();
    if (disks.isNull()) disks = d["diskinfo"].as<JsonArray>();
    if (!disks.isNull() && disks.size() > 0) {
        diskApi = true;
        for (JsonVariant dv : disks) {
            if (diskCount >= SY_MAX_DISKS) break;
            DiskInfo &di = dlist[diskCount];

            setStr(di.name, sizeof(di.name), dv["diskno"] | (dv["name"] | ""));
            setStr(di.model, sizeof(di.model), dv["model"] | "");
            setStr(di.status, sizeof(di.status), dv["status"] | (dv["overview_status"] | ""));
            setStr(di.pool, sizeof(di.pool), dv["volume"] | "");
            di.capacity = jsonU64(dv["capacity"]);
            if (di.capacity == 0) di.capacity = jsonU64(dv["size_total"]);

            bool tOk = false;
            int t = jsonInt(dv["temp"], tOk);
            di.temp = (tOk && t >= 0) ? (int16_t)clampInt(t, -1, 200) : -1;

            if (di.temp > diskTemp) diskTemp = di.temp;
            if (di.status[0] && strcmp(di.status, "normal") != 0) diskWarn++;
            diskCount++;
        }
    }

    if (DataLock(80)) {
        memcpy(s_data.vols, vols, sizeof(vols));
        s_data.volCount = n;
        s_data.storageValid = true;
        s_data.diskApiValid = diskApi;
        s_data.diskCount = diskCount;
        s_data.diskWarn = diskWarn;
        s_data.diskTempMax = diskTemp;
        memcpy(s_data.disks, dlist, sizeof(dlist));
        s_data.storageSeq++;
        s_data.lastSlowMs = millis();
        DataUnlock();
    }
    return 0;
}

static int fetchUps()
{
    if (!s_apiUps.present) {
        if (DataLock(80)) {
            s_data.upsState = UPS_UNSUPPORTED;
            s_data.upsChargeValid = false;
            s_data.upsRuntimeValid = false;
            s_data.upsSeq++;
            DataUnlock();
        }
        return 0;
    }

    String q = apiPath(s_apiUps);
    q += "?api=SYNO.Core.ExternalDevice.UPS&method=get&version=";
    q += String((unsigned)s_apiUps.useVer);
    q = withSid(q);

    DynamicJsonDocument doc(2048);
    int rc = apiCall(q, "", doc);
    if (rc == 119) return rc;

    if (rc == 102 || rc == 103 || rc == 104) {
        if (DataLock(80)) {
            s_data.upsState = UPS_UNSUPPORTED;
            s_data.upsChargeValid = false;
            s_data.upsRuntimeValid = false;
            s_data.upsSeq++;
            DataUnlock();
        }
        return 0;
    }
    if (rc == 105) {
        if (DataLock(80)) {
            s_data.upsState = UPS_NO_PERMISSION;
            s_data.upsChargeValid = false;
            s_data.upsRuntimeValid = false;
            s_data.upsSeq++;
            DataUnlock();
        }
        return 0;
    }
    if (rc) return rc;

    JsonObject d = doc["data"].as<JsonObject>();
    if (d.isNull()) return ERR_SHAPE;

    /* The payload differs between DSM versions: the list may be called
     * usb_ups, or the fields may sit directly in data. */
    JsonObject up;
    JsonVariant raw = d["usb_ups"];
    if (raw.is<JsonArray>()) {
        JsonArray arr = raw.as<JsonArray>();
        if (arr.size() > 0) up = arr[0].as<JsonObject>();
    } else if (raw.is<JsonObject>()) {
        up = raw.as<JsonObject>();
    }
    if (up.isNull() && (d.containsKey("status") || d.containsKey("charge") ||
                        d.containsKey("battery_charge") || d.containsKey("runtime"))) {
        up = d;
    }

    UpsState state = UPS_NONE;
    bool chargeValid = false;
    int charge = 0;
    bool rtValid = false;
    int32_t rt = 0;
    char model[24] = { 0 };
    char rawStatus[20] = { 0 };

    /* DSM reports usb_ups_connect=false when no UPS is attached, even though
     * the API still answers with a status word. */
    if (!up.isNull()) {
        JsonVariant connV = up["usb_ups_connect"];
        if (!connV.isNull() && !jsonBool(connV)) up = JsonObject();
    }

    if (!up.isNull()) {
        const char *st = up["status"] | "";
        if (!st[0]) st = up["battery_status"] | "";

        /* Status arrives as "usb_ups_status_online" / "..._onbattery";
         * keep only the meaningful tail for display. */
        const char *stShort = st;
        const char *prefix = "usb_ups_status_";
        size_t prefixLen = strlen(prefix);
        if (strncmp(st, prefix, prefixLen) == 0) stShort = st + prefixLen;
        setStr(rawStatus, sizeof(rawStatus), stShort);

        String s = st;
        s.toLowerCase();
        if (s.length() == 0) {
            state = UPS_UNKNOWN;
        } else if (s.indexOf("low") >= 0) {
            state = UPS_LOW_BATTERY;
        } else if (s.indexOf("batt") >= 0 || s == "ob" || s.indexOf("discharg") >= 0) {
            state = UPS_BATTERY;
        } else if (s.indexOf("line") >= 0 || s.indexOf("online") >= 0 || s == "ol" ||
                   s.indexOf("ac") == 0 || s.indexOf("charg") >= 0) {
            state = UPS_LINE;
        } else {
            state = UPS_UNKNOWN;
        }

        static const char *chargeKeys[] = { "charge", "battery_charge", "battery_level",
                                            "capacity", "remain_charge" };
        for (size_t i = 0; i < sizeof(chargeKeys) / sizeof(chargeKeys[0]); i++) {
            bool ok = false;
            int v = jsonInt(up[chargeKeys[i]], ok);
            if (ok) {
                charge = clampInt(v, 0, 100);
                chargeValid = true;
                break;
            }
        }

        static const char *rtKeys[] = { "runtime", "remain_time", "battery_runtime",
                                        "est_runtime", "remaining_time" };
        for (size_t i = 0; i < sizeof(rtKeys) / sizeof(rtKeys[0]); i++) {
            bool ok = false;
            int v = jsonInt(up[rtKeys[i]], ok);
            if (ok && v > 0) {
                rt = (int32_t)v;
                rtValid = true;
                break;
            }
        }

        const char *m = up["model"] | (up["model_name"] | (up["product"] | ""));
        if (!m[0]) m = up["manufacture"] | "";
        setStrTrim(model, sizeof(model), m);
    }

    if (DataLock(80)) {
        s_data.upsState = state;
        s_data.upsChargeValid = chargeValid;
        s_data.upsCharge = (int16_t)charge;
        s_data.upsRuntimeValid = rtValid;
        s_data.upsRuntime = rt;
        setStr(s_data.upsModel, sizeof(s_data.upsModel), model);
        setStr(s_data.upsRaw, sizeof(s_data.upsRaw), rawStatus);
        s_data.upsSeq++;
        DataUnlock();
    }
    return 0;
}

/* ------------------------------------------------------- connected users */
/* "2026/10/28 02:08:08" (first_login_time) -> time_t.
 * Both this and systime are NAS local time; the same mktime offset is applied
 * to both, so the derived duration is correct even if the device TZ differs. */
static time_t parseDateTime(const char *s)
{
    int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
    if (!s || sscanf(s, "%d/%d/%d %d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
    if (Y < 1970 || M < 1 || M > 12 || D < 1 || D > 31) return 0;

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = Y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min = m;
    t.tm_sec = sec;
    t.tm_isdst = 0;
    return mktime(&t);
}

/* "Wed Oct 28 03:36:16 2026" (data.systime) -> time_t */
static time_t parseSysTime(const char *s)
{
    char wd[8] = { 0 }, mon[8] = { 0 };
    int D = 0, h = 0, m = 0, sec = 0, Y = 0;
    if (!s || sscanf(s, "%7s %7s %d %d:%d:%d %d", wd, mon, &D, &h, &m, &sec, &Y) != 7) return 0;

    static const char *MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int mi = -1;
    for (int i = 0; i < 12; i++) {
        if (strncmp(mon, MON[i], 3) == 0) { mi = i; break; }
    }
    if (mi < 0 || Y < 1970) return 0;

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = Y - 1900;
    t.tm_mon = mi;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min = m;
    t.tm_sec = sec;
    t.tm_isdst = 0;
    return mktime(&t);
}

/* SYNO.Core.CurrentConnection list: verified on DSM 7.3.
 * Only method=list exists - delete/remove/kick/logout all answer error 103,
 * so this app can show the sessions but cannot terminate them. */
static int fetchUsers()
{
    String q = apiPath(s_apiUsers);
    q += "?api=SYNO.Core.CurrentConnection&method=list&version=";
    q += String((unsigned)s_apiUsers.useVer);
    q = withSid(q);

    /* The 8-session payload from this DSM is ~3.1 KB of JSON but needs an
     * 8 KB document (every key and value is stored again in the pool); 4 KB
     * ends in NoMemory and would leave the page empty. */
    DynamicJsonDocument doc(8192);
    int rc = apiCall(q, "", doc);
    if (rc) {
        if (DataLock(30)) { s_data.usersErr = (int16_t)rc; DataUnlock(); }
        return rc;
    }

    JsonObject d = doc["data"].as<JsonObject>();
    if (d.isNull()) {
        if (DataLock(30)) { s_data.usersErr = ERR_SHAPE; DataUnlock(); }
        return ERR_SHAPE;
    }

    time_t nowT = parseSysTime(d["systime"] | "");
    char nasNow[24] = { 0 };
    setStr(nasNow, sizeof(nasNow), d["systime"] | "");

    SessionInfo list[SY_MAX_SESSIONS];
    memset(list, 0, sizeof(list));
    uint8_t n = 0;

    for (JsonObject it : d["items"].as<JsonArray>()) {
        if (n >= SY_MAX_SESSIONS) break;
        const char *who = it["who"] | "";
        if (!who[0]) continue;

        setStr(list[n].who, sizeof(list[n].who), who);

        const char *proto = it["protocol"] | "";
        if (!proto[0]) proto = it["type"] | "";
        setStr(list[n].proto, sizeof(list[n].proto), proto);

        setStr(list[n].from, sizeof(list[n].from), it["from"] | "");
        list[n].kickable = jsonBool(it["can_be_kicked"]);

        time_t login = parseDateTime(it["first_login_time"] | "");
        list[n].seconds = (nowT > 0 && login > 0 && nowT >= login)
                        ? (uint32_t)(nowT - login) : 0xFFFFFFFFu;
        n++;
    }

    if (DataLock(80)) {
        memcpy(s_data.users, list, sizeof(list));
        s_data.userCount = n;
        s_data.usersValid = true;
        s_data.usersErr = 0;
        s_data.usersSeq++;
        setStr(s_data.nasTime, sizeof(s_data.nasTime), nasNow);
        DataUnlock();
    }
    return 0;
}

/* --------------------------------------------------------------- session */
static bool doLogin(const Config &c, int &err)
{
    err = 0;
    String body = "api=SYNO.API.Auth&version=";
    body += String((unsigned)s_apiAuth.useVer);
    body += "&method=login&account=";
    body += urlEncode(c.user);
    body += "&passwd=";
    body += urlEncode(c.pass);
    body += "&session=Core&format=sid";

    DynamicJsonDocument doc(1024);
    String path = apiPath(s_apiAuth);
    int rc = apiCall(path, body, doc);
    if (rc) {
        err = rc;
        return false;
    }
    const char *sidRaw = doc["data"]["sid"] | "";
    String sid(sidRaw);
    if (sid.length() == 0) {
        err = ERR_SHAPE;
        return false;
    }
    s_sess.sid = sid;
    s_sess.valid = true;
    s_sess.since = (uint32_t)time(nullptr);
    s_sess.sinceMs = millis();
    return true;
}

static void doLogout()
{
    if (!s_sess.valid) return;
    /* Logout is supported at version 6 even though login uses 3; stay inside
     * the range the NAS advertises for SYNO.API.Auth. */
    int v = clampInt(6, s_apiAuth.minVer, s_apiAuth.maxVer);
    String q = apiPath(s_apiAuth);
    q += "?api=SYNO.API.Auth&version=";
    q += String((unsigned)v);
    q += "&method=logout";
    q = withSid(q);
    DynamicJsonDocument doc(512);
    apiCall(q, "", doc);
    s_sess.valid = false;
    s_sess.sid = "";
}

/* Ask DSM which APIs exist and what their current version is. This keeps the
 * app working across DSM versions instead of assuming method/version. */
static void queryApiInfo()
{
    String q = "/webapi/query.cgi?api=SYNO.API.Info&version=1&method=query&query=";
    q += "SYNO.API.Auth,SYNO.Core.System,SYNO.Core.System.Utilization,";
    q += "SYNO.Core.ExternalDevice.UPS,SYNO.Virtualization.Cluster,";
    q += "SYNO.Core.CurrentConnection";

    DynamicJsonDocument doc(3072);
    int rc = apiCall(q, "", doc);
    if (rc) {
        printf("[Syno] API discovery failed (%d), using defaults\n", rc);
        return;
    }

    JsonObject d = doc["data"].as<JsonObject>();
    if (d.isNull()) return;

    /* pref = version this build knows how to talk, used when the NAS offers
     * a range that contains it. */
    struct KeyMap {
        const char *key;
        ApiEntry   *entry;
        uint8_t     pref;
    } map[] = {
        { "SYNO.API.Auth", &s_apiAuth, 3 },
        { "SYNO.Core.CurrentConnection", &s_apiUsers, 1 },
        { "SYNO.Core.System", &s_apiSys, 3 },
        { "SYNO.Core.System.Utilization", &s_apiUtil, 1 },
        { "SYNO.Core.ExternalDevice.UPS", &s_apiUps, 1 },
        { "SYNO.Virtualization.Cluster", &s_apiVmm, 1 },
    };

    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        ApiEntry *e = map[i].entry;
        JsonVariant v = d[map[i].key];
        if (v.isNull()) {
            e->present = false;
            printf("[Syno] %s not advertised by this DSM\n", map[i].key);
            continue;
        }
        e->present = true;

        const char *path = v["path"] | "entry.cgi";
        setStr(e->path, sizeof(e->path), path);

        bool okMin = false, okMax = false;
        int mn = jsonInt(v["minVersion"], okMin);
        int mx = jsonInt(v["maxVersion"], okMax);
        if (!okMin) mn = 1;
        if (!okMax) mx = map[i].pref;
        mn = clampInt(mn, 1, 20);
        mx = clampInt(mx, 1, 20);
        if (mn > mx) mn = mx;

        e->minVer = (uint8_t)mn;
        e->maxVer = (uint8_t)mx;
        e->useVer = (uint8_t)clampInt(map[i].pref, mn, mx);

        printf("[Syno] %s path=%s v%d..%d -> using v%u\n", map[i].key, e->path,
               mn, mx, (unsigned)e->useVer);
    }
}

/* ------------------------------------------------------- connection test */
static void runConnectionTest()
{
    Config c;
    if (!DataLock(200)) return;
    c = s_testCfg;
    DataUnlock();

    if (DataLock(50)) {
        s_data.testState = 1;
        setStr(s_data.testMsg, sizeof(s_data.testMsg), "Testing...");
        DataUnlock();
    }

    char msg[64];
    int err = 0;
    bool ok = false;

    if (!s_dev || !s_dev->Wf.isConnected()) {
        snprintf(msg, sizeof(msg), "Wi-Fi is not connected");
    } else if (!c.configured()) {
        snprintf(msg, sizeof(msg), "Address and account required");
    } else {
        String savedSid = s_sess.sid;
        bool savedValid = s_sess.valid;

        if (doLogin(c, err)) {
            ok = true;
            /* Read the model/DSM string so the user sees the right device. */
            String q = apiPath(s_apiSys);
            q += "?api=SYNO.Core.System&method=info&version=";
            q += String((unsigned)s_apiSys.useVer);
            q = withSid(q);
            DynamicJsonDocument doc(3072);
            int rc = apiCall(q, "", doc);
            if (rc == 0) {
                const char *model = doc["data"]["model"] | "";
                const char *fw = doc["data"]["firmware_ver"] | "";
                if (model[0] && fw[0])
                    snprintf(msg, sizeof(msg), "OK: %s / %s", model, fw);
                else if (model[0])
                    snprintf(msg, sizeof(msg), "OK: %s", model);
                else
                    snprintf(msg, sizeof(msg), "OK: login accepted");
            } else {
                snprintf(msg, sizeof(msg), "Login OK, info denied (%d)", rc);
            }
            doLogout();
            s_sess.sid = savedSid;
            s_sess.valid = savedValid;
        } else {
            char e[40];
            fmtErr(e, sizeof(e), err);
            snprintf(msg, sizeof(msg), "Failed: %s", e);
        }
    }

    printf("[Syno] connection test: %s\n", ok ? "ok" : "failed");

    if (DataLock(50)) {
        s_data.testState = ok ? 2 : 3;
        setStr(s_data.testMsg, sizeof(s_data.testMsg), msg);
        DataUnlock();
    }
}

/* ------------------------------------------------------------ worker task */
static bool stopRequested(uint32_t myGen)
{
    return s_stopGen == myGen;
}

static void netTask(void *arg)
{
    uint32_t myGen = (uint32_t)(uintptr_t)arg;
    taskCountAdd(1);

    bool     loggedIn = false;
    bool     apisKnown = false;
    uint32_t lastFast = 0;
    uint32_t lastSlow = 0;
    uint32_t retryAt = 0;
    uint32_t retryDelay = SY_RETRY_MIN_MS;
    bool     firstRun = true;

    while (!stopRequested(myGen)) {
        uint32_t cmds = takeCommands();
        uint32_t now = millis();

        if (cmds & CMD_TEST) runConnectionTest();

        if (!s_dev || !s_dev->Wf.isConnected()) {
            loggedIn = false;
            s_sess.valid = false;
            publishConn(CONN_NO_WIFI, "Wi-Fi is not connected");
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        if (cmds & CMD_RELOGIN) {
            doLogout();
            loggedIn = false;
            apisKnown = false;
            retryDelay = SY_RETRY_MIN_MS;
            retryAt = 0;
            firstRun = true;
        }

        if (!loggedIn) {
            if ((int32_t)(now - retryAt) < 0) {
                vTaskDelay(pdMS_TO_TICKS(150));
                continue;
            }

            Config c;
            CfgGet(c);
            if (!c.configured()) {
                publishConn(CONN_OFFLINE, "Set the NAS address in Settings");
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            /* Show "connecting" before the first request: API discovery can
             * take a few seconds on an unreachable NAS. */
            publishConn(CONN_CONNECTING, "");

            /* API paths/versions first (no SID needed), then log in with the
             * path the NAS advertises for SYNO.API.Auth. */
            if (!apisKnown) {
                queryApiInfo();
                apisKnown = true;
            }

            int err = 0;
            if (doLogin(c, err)) {
                loggedIn = true;
                retryDelay = SY_RETRY_MIN_MS;
                lastFast = 0;
                lastSlow = 0;
                firstRun = true;
                publishConn(CONN_ONLINE, "");
                printf("[Syno] logged in\n");
            } else {
                char m[64];
                fmtErr(m, sizeof(m), err);
                publishConn(authError(err) ? CONN_AUTH_FAIL : CONN_ERROR, m);
                retryAt = millis() + retryDelay;
                retryDelay = retryDelay * 2;
                if (retryDelay > SY_RETRY_MAX_MS) retryDelay = SY_RETRY_MAX_MS;
                printf("[Syno] login failed (%d), retry in %u ms\n", err, (unsigned)retryDelay);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
        }

        /* The overview shows CPU/RAM/network too, so it polls as fast as the
         * home page; storage/system/UPS pages refresh the slow data sooner. */
        uint8_t page = s_page;
        bool fastPage = (page == PAGE_HOME || page == PAGE_OVERVIEW);
        bool slowPage = (page == PAGE_OVERVIEW || page == PAGE_STORAGE ||
                         page == PAGE_SYSTEM || page == PAGE_UPS);
        uint32_t fastIv = fastPage ? SY_FAST_ACTIVE_MS : SY_FAST_IDLE_MS;
        uint32_t slowIv = slowPage ? SY_SLOW_ACTIVE_MS : SY_SLOW_HOME_MS;

        if (firstRun || (cmds & CMD_FAST) || (millis() - lastFast >= fastIv)) {
            int rc = fetchFast();
            if (rc == 119) {
                s_sess.valid = false;
                loggedIn = false;
                retryAt = millis() + 1000;   /* one immediate re-login attempt */
                publishConn(CONN_CONNECTING, "Session expired, signing in again");
                printf("[Syno] session expired\n");
                lastFast = millis();
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            if (rc && rc != 102 && rc != 119) logApiError("utilization", rc);
            if (rc == 102) {
                if (DataLock(30)) {
                    setStr(s_data.msg, sizeof(s_data.msg), "CPU/RAM API not available");
                    DataUnlock();
                }
            }
            if (rc && rc != 102) {
                char m[64];
                fmtErr(m, sizeof(m), rc);
                if (rc == ERR_NET || rc == ERR_HTTP || rc == ERR_NOTJSON)
                    publishConn(CONN_ERROR, m);
                publishFastError(rc);
            } else if (rc == 0) {
                publishConn(CONN_ONLINE, "");
            }
            lastFast = millis();
        }

        if (firstRun || (cmds & CMD_SLOW) || (millis() - lastSlow >= slowIv)) {
            bool expired = false;
            int src = 0;
            src = fetchSystemInfo();
            if (src == 119) expired = true;
            else if (src) logApiError("system.info", src);

            if (!expired) {
                src = fetchNetworkInfo();
                if (src == 119) expired = true;
                else if (src) logApiError("system.network", src);
            }
            if (!expired) {
                src = fetchStorage();
                if (src == 119) expired = true;
                else if (src) logApiError("system.storage", src);
            }
            if (!expired) {
                src = fetchUps();
                if (src == 119) expired = true;
                else if (src) logApiError("ups", src);
            }
            if (!expired) {
                bool haveUsers = false;
                if (DataLock(20)) { haveUsers = s_data.usersValid; DataUnlock(); }
                /* only while the page is open, or the first time ever */
                if (page == PAGE_OVERVIEW || !haveUsers) {
                    src = fetchUsers();
                    if (src == 119) expired = true;
                    else if (src) logApiError("users", src);
                }
            }

            if (expired) {
                s_sess.valid = false;
                loggedIn = false;
                retryAt = millis() + 1000;
                publishConn(CONN_CONNECTING, "Session expired, signing in again");
                printf("[Syno] session expired\n");
            }
            lastSlow = millis();
        }

        firstRun = false;
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    /* Leave the DSM session clean when possible. */
    if (s_sess.valid && s_dev && s_dev->Wf.isConnected()) doLogout();

    taskCountAdd(-1);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------- API */
void NetStart(void *bsp)
{
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    if (!s_httpMtx) s_httpMtx = xSemaphoreCreateMutex();
    if (!s_mtx || !s_httpMtx) {
        printf("[Syno] cannot create mutex\n");
        return;
    }

    s_dev = (CHAPPIE *)bsp;

    /* Wait for a previous worker (app re-entry) so only one polls. */
    for (int i = 0; i < 30 && taskCountGet() > 0; i++) vTaskDelay(pdMS_TO_TICKS(100));

    if (DataLock(200)) {
        s_data = Snapshot();
        DataUnlock();
    }

    CfgLoad();
    s_sess.valid = false;
    s_sess.sid = "";
    s_rate.have = false;
    s_rate.counter = false;

    apiEntryReset(s_apiUsers, 1);
    apiEntryReset(s_apiAuth, 3);
    apiEntryReset(s_apiSys, 3);
    apiEntryReset(s_apiUtil, 1);
    apiEntryReset(s_apiUps, 1);
    apiEntryReset(s_apiVmm, 1);

    /* Do not reset s_stopGen here: a previous worker that is still finishing
     * a request must keep seeing its own stop request. The new generation
     * simply differs from it. */
    uint32_t gen = ++s_gen;

    BaseType_t ok = xTaskCreatePinnedToCore(netTask, "SyNet", 10240,
                                            (void *)(uintptr_t)gen, 5, nullptr, 1);
    if (ok != pdPASS) {
        printf("[Syno] worker task create failed\n");
        publishConn(CONN_ERROR, "Out of memory for network task");
    }
}

void NetStop()
{
    s_stopGen = s_gen;

    for (int i = 0; i < 30; i++) {
        if (taskCountGet() == 0) return;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* The worker never touches LVGL, so letting it finish on its own is safe:
     * it only writes the static snapshot and then deletes itself. */
    printf("[Syno] worker still finishing a request\n");
}

void NetApplyConfig()
{
    s_rate.have = false;
    s_rate.counter = false;
    postCommand(CMD_RELOGIN | CMD_FAST);
}

void NetTest(const Config &c)
{
    Config t = c;
    cfgSanitize(t);
    t.user.trim();

    if (DataLock(200)) {
        s_testCfg = t;
        s_data.testState = 1;
        setStr(s_data.testMsg, sizeof(s_data.testMsg), "Testing...");
        DataUnlock();
    }
    postCommand(CMD_TEST);
}

void NetSetPage(uint8_t page)
{
    if (page >= PAGE_MAX) page = PAGE_HOME;
    s_page = page;
}

void NetRequestFast()
{
    /* The UI asks for fresh data when a page becomes visible: refresh the
     * utilization poll and the slow system/storage/UPS data in one go. */
    postCommand(CMD_FAST | CMD_SLOW);
}

} // namespace Sy
