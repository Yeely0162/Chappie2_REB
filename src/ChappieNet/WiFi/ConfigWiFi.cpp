#include "ConfigWiFi.h"

#include <Preferences.h>
#include <esp_random.h>
#include <lwip/apps/sntp.h>
#include <time.h>

namespace {

constexpr const char *CONFIG_NS = "WiFiAP";
constexpr const char *SETTINGS_NS = "sys";
constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t CONNECT_DELAY_AFTER_HTTP_MS = 500;
constexpr uint32_t AP_CLOSE_AFTER_CONNECT_MS = 4000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 15000;
constexpr uint32_t NTP_RETRY_DELAY_MS = 60000;
constexpr uint32_t NTP_PERIODIC_MS = 6UL * 60UL * 60UL * 1000UL;

/* The Arduino async scan watchdog is max_ms_per_chan * 20 internally. 120 ms
 * gave only 2.4 s, which an AP+STA scan easily overruns, producing a bogus
 * "Scan failed" while the driver was still scanning. 400 ms gives an 8 s
 * internal budget; our own limit is deliberately longer. */
constexpr uint32_t SCAN_MS_PER_CHANNEL = 400;
constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
constexpr uint32_t SCAN_RETRY_DELAY_MS = 1500;

const char *NTP_SERVERS[] = {
    "ntp.aliyun.com",
    "pool.ntp.org",
    "time.cloudflare.com",
    "time.google.com",
};
constexpr uint8_t NTP_SERVER_COUNT = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);

class LockGuard
{
public:
    explicit LockGuard(SemaphoreHandle_t m) : _m(m)
    {
        if (_m)
            xSemaphoreTake(_m, portMAX_DELAY);
    }
    ~LockGuard()
    {
        if (_m)
            xSemaphoreGive(_m);
    }

private:
    SemaphoreHandle_t _m;
};

} // namespace

ConfigWiFi::ConfigWiFi()
{
    _lock = xSemaphoreCreateMutex();
    _scanLock = xSemaphoreCreateMutex();
}

ConfigWiFi::~ConfigWiFi()
{
    if (_serverStarted) {
        _server.close();
        _serverStarted = false;
    }
    _dns.stop();
}

/* ------------------------------------------------------------------------- */
/* Init / configuration                                                       */
/* ------------------------------------------------------------------------- */

void ConfigWiFi::Init(I2C_BM8563 *rtc)
{
    _rtc = rtc;

    WiFi.persistent(false);
    WiFi.setHostname("Chappie-Yeely");
    WiFi.mode(WIFI_OFF);

    _loadConfig();

    /* Cache the MAC while the interface is known, so the UI never sees a
     * dangling pointer from a temporary String. */
    String mac = WiFi.macAddress();
    if (mac.length() > 0 && mac != "00:00:00:00:00:00")
        _macCache = mac.c_str();
    else
        _macCache = "None";

    _radioOn = false;
    _apOn = false;
    _connected = false;
    _staConnecting = false;
    wifiEnabled = false;
    OnAP = false;
    _setMessage("Wi-Fi off");

    /* Bind the portal routes once. The server itself is started/stopped by
     * the state machine, so registrations survive restarts. */
    _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
    _server.on("/scan", HTTP_GET, [this]() { _handleScanStart(); });
    _server.on("/scan_results", HTTP_GET, [this]() { _handleScanResults(); });
    _server.on("/connect", HTTP_POST, [this]() { _handleConnect(); });
    _server.on("/connect", HTTP_GET, [this]() { _handleConnect(); });
    _server.on("/status", HTTP_GET, [this]() { _handleStatus(); });
    _server.onNotFound([this]() {
        if (_server.method() == HTTP_GET)
            _handleRoot();
        else
            _server.send(404, "text/plain", "Not found");
    });

    WiFi_LOG("[WiFi] init, configured=%s\n", _ssid.empty() ? "<none>" : _ssid.c_str());
}

void ConfigWiFi::_loadConfig()
{
    Preferences prefs;
    if (prefs.begin(CONFIG_NS, true)) {
        String s = prefs.getString("WiFiName", "");
        String p = prefs.getString("WiFiPass", "");
        _ssid = s.c_str();
        _pass = p.c_str();
        prefs.end();
    }
    if (prefs.begin(SETTINGS_NS, true)) {
        uint8_t idx = prefs.getUChar("ntpIdx", 0);
        if (idx < NTP_SERVER_COUNT)
            _ntpIndex = idx;
        AutoTime = prefs.getBool("ntpAuto", true);
        prefs.end();
    }
}

void ConfigWiFi::_setMessage(const char *msg)
{
    LockGuard guard(_lock);
    _message = (msg && msg[0]) ? msg : "";
}

void ConfigWiFi::_setNtpError(const char *msg)
{
    LockGuard guard(_lock);
    snprintf(_ntpError, sizeof(_ntpError), "%s", (msg && msg[0]) ? msg : "");
    _ntpHasError = (_ntpError[0] != '\0');
}

void ConfigWiFi::_setScanError(const char *msg)
{
    LockGuard guard(_lock);
    snprintf(_scanError, sizeof(_scanError), "%s", (msg && msg[0]) ? msg : "");
}

String ConfigWiFi::lastErrorText()
{
    LockGuard guard(_lock);
    return String(_message.c_str());
}

String ConfigWiFi::configuredSSID()
{
    LockGuard guard(_lock);
    return String(_ssid.c_str());
}

String ConfigWiFi::scanErrorText()
{
    LockGuard guard(_lock);
    return String(_scanError);
}

String ConfigWiFi::ntpLastErrorText()
{
    LockGuard guard(_lock);
    return String(_ntpError);
}

void ConfigWiFi::_saveCredentials(const String &ssid, const String &pass)
{
    Preferences prefs;
    if (!prefs.begin(CONFIG_NS, false)) {
        printf("[WiFi] failed to open config namespace\n");
        return;
    }
    prefs.putString("WiFiName", ssid);
    prefs.putString("WiFiPass", pass);
    prefs.end();
    /* Never log the password. */
    WiFi_LOG("[WiFi] credentials saved for '%s'\n", ssid.c_str());
}

void ConfigWiFi::setAutoTime(bool on)
{
    /* Preferences and the SNTP API are applied in update() on the main task. */
    _post(on ? Cmd::NtpAutoOn : Cmd::NtpAutoOff);
}

void ConfigWiFi::setNtpServerIndex(uint8_t index)
{
    if (index >= NTP_SERVER_COUNT)
        return;
    LockGuard guard(_lock);
    _cmd = Cmd::NtpServer;
    _cmdIndex = index;
}

/* ------------------------------------------------------------------------- */
/* Requests (any task)                                                        */
/* ------------------------------------------------------------------------- */

void ConfigWiFi::_post(Cmd cmd, const String *ssid, const String *pass, bool closeAp)
{
    LockGuard guard(_lock);
    _cmd = cmd;
    if (ssid)
        _cmdSsid = ssid->c_str();
    else
        _cmdSsid.clear();
    if (pass)
        _cmdPass = pass->c_str();
    else
        _cmdPass.clear();
    _cmdCloseAp = closeAp;
}

void ConfigWiFi::enableWiFi()
{
    _post(Cmd::Enable);
}

void ConfigWiFi::disableWiFi()
{
    _post(Cmd::Disable);
}

void ConfigWiFi::APMode()
{
    _post(Cmd::ApStart);
}

bool ConfigWiFi::APClose()
{
    _post(Cmd::ApStop);
    return true;
}

void ConfigWiFi::requestToggle()
{
    /* Snapshot the state under the command lock. */
    LockGuard guard(_lock);
    if (_radioOn || _apOn)
        _cmd = Cmd::Disable;
    else
        _cmd = Cmd::Enable;
}

void ConfigWiFi::requestConnect(const String &ssid, const String &password, bool closeApAfterConnect)
{
    _post(Cmd::Connect, &ssid, &password, closeApAfterConnect);
}

void ConfigWiFi::requestDisconnect()
{
    _post(Cmd::Disconnect);
}

void ConfigWiFi::requestForget()
{
    _post(Cmd::Forget);
}

void ConfigWiFi::requestScan()
{
    /* One scan task at a time. The device button and the phone portal share
     * this entry point and can be tapped in parallel; a second request while
     * a scan is pending or running used to leave _scanPending set, which made
     * _updateScan() start another scan right after the first one finished. */
    if (_scanRunning || _scanPending) {
        printf("[WiFi] scan request ignored: already %s\n",
               _scanRunning ? "running" : "pending");
        return;
    }

    /* Mark pending immediately so the polling endpoint reports "scanning"
     * before the command is applied on the next update(). */
    _scanRequestedAt = millis();
    _scanPending = true;
    _post(Cmd::Scan);
}

void ConfigWiFi::requestNtpSync()
{
    LockGuard guard(_lock);
    _ntpManual = true;
}

/* ------------------------------------------------------------------------- */
/* State machine                                                              */
/* ------------------------------------------------------------------------- */

void ConfigWiFi::update()
{
    _applyCommand();
    _updateSta();
    _updateScan();
    _updateNtp();

    if (_apOn) {
        _server.handleClient();
        _dns.processNextRequest();
        _apClients = (uint8_t)WiFi.softAPgetStationNum();
    } else {
        _apClients = 0;
    }
}

void ConfigWiFi::_applyCommand()
{
    Cmd cmd;
    std::string ssid;
    std::string pass;
    bool closeAp = false;
    uint8_t cmdIndex = 0;

    {
        LockGuard guard(_lock);
        cmd = _cmd;
        _cmd = Cmd::None;
        ssid = _cmdSsid;
        pass = _cmdPass;
        closeAp = _cmdCloseAp;
        cmdIndex = _cmdIndex;
        _cmdSsid.clear();
        _cmdPass.clear();
        _cmdCloseAp = false;
        _cmdIndex = 0;
    }

    if (cmd == Cmd::None)
        return;

    switch (cmd) {
    case Cmd::Enable: {
        String savedSsid;
        String savedPass;
        {
            LockGuard guard(_lock);
            savedSsid = _ssid.c_str();
            savedPass = _pass.c_str();
        }
        if (savedSsid.length() > 0) {
            _doConnect(savedSsid, savedPass, false);
        } else {
            printf("[WiFi] no saved credentials, starting setup portal\n");
            _startPortal();
            _setMessage("No saved network - use the setup hotspot");
        }
        break;
    }

    case Cmd::Disable:
        _staConnecting = false;
        _connected = false;
        _error = false;
        _apCloseAt = 0;
        if (_apOn)
            _stopPortal();
        WiFi.disconnect(true, false);
        _radioOn = false;
        wifiEnabled = false;
        _applyMode();
        _setMessage("Wi-Fi off");
        printf("[WiFi] disabled\n");
        break;

    case Cmd::Connect:
        if (ssid.empty()) {
            _error = true;
            _setMessage("Empty SSID");
            break;
        }
        _saveCredentials(String(ssid.c_str()), String(pass.c_str()));
        {
            LockGuard guard(_lock);
            _ssid = ssid;
            _pass = pass;
        }
        _doConnect(String(ssid.c_str()), String(pass.c_str()), closeAp);
        break;

    case Cmd::Disconnect:
        WiFi.disconnect(true, false);
        _staConnecting = false;
        _connected = false;
        _apCloseAt = 0;
        _error = false;
        if (!_apOn) {
            _radioOn = false;
            _applyMode();
            _setMessage("Wi-Fi off");
        } else {
            _applyMode();
            _setMessage("AP mode");
        }
        break;

    case Cmd::Forget: {
        Preferences prefs;
        if (prefs.begin(CONFIG_NS, false)) {
            prefs.remove("WiFiName");
            prefs.remove("WiFiPass");
            prefs.end();
        }
        {
            LockGuard guard(_lock);
            _ssid.clear();
            _pass.clear();
        }
        WiFi.disconnect(true, false);
        _connected = false;
        _staConnecting = false;
        _error = false;
        _apCloseAt = 0;
        _radioOn = _apOn;
        wifiEnabled = _radioOn;
        _applyMode();
        _setMessage(_apOn ? "Saved network cleared (AP active)" : "Wi-Fi off");
        printf("[WiFi] saved credentials cleared\n");
        break;
    }

    case Cmd::ApStart:
        _startPortal();
        break;

    case Cmd::ApStop:
        _stopPortal();
        break;

    case Cmd::Scan:
        if (!_radioOn) {
            _setScanError("Wi-Fi is off");
            _scanPending = false;
            break;
        }
        /* _updateScan() starts the actual async scan on this same cycle. */
        _scanPending = true;
        break;

    case Cmd::NtpAutoOn: {
        AutoTime = true;
        Preferences prefs;
        if (prefs.begin(SETTINGS_NS, false)) {
            prefs.putBool("ntpAuto", true);
            prefs.end();
        }
        if (_ntpState == NtpUiState::Disabled)
            _ntpState = NtpUiState::Idle;
        break;
    }

    case Cmd::NtpAutoOff: {
        AutoTime = false;
        Preferences prefs;
        if (prefs.begin(SETTINGS_NS, false)) {
            prefs.putBool("ntpAuto", false);
            prefs.end();
        }
        sntp_stop();
        _ntpState = NtpUiState::Disabled;
        _setNtpError("");
        break;
    }

    case Cmd::NtpServer:
        if (cmdIndex < NTP_SERVER_COUNT) {
            _ntpIndex = cmdIndex;
            Preferences prefs;
            if (prefs.begin(SETTINGS_NS, false)) {
                prefs.putUChar("ntpIdx", cmdIndex);
                prefs.end();
            }
            sntp_stop();
            _ntpState = AutoTime ? NtpUiState::Idle : NtpUiState::Disabled;
            _setNtpError("");
            WiFi_LOG("[NTP] server set to %s\n", NTP_SERVERS[cmdIndex]);
        }
        break;

    default:
        break;
    }
}

void ConfigWiFi::_doConnect(const String &ssid, const String &pass, bool closeAp)
{
    _radioOn = true;
    wifiEnabled = true;
    _error = false;
    _staConnecting = true;
    _connected = false;
    _closeApAfterConnect = closeAp;
    _apCloseAt = 0;
    _connectStart = millis();
    _connectDeadline = _connectStart + STA_CONNECT_TIMEOUT_MS;
    _connectAt = _connectStart + CONNECT_DELAY_AFTER_HTTP_MS;
    _setMessage("Connecting...");

    _applyMode();
    WiFi.disconnect(false, false);
    WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
    WiFi_LOG("[WiFi] connecting to '%s'\n", ssid.c_str());
}

void ConfigWiFi::_updateSta()
{
    const uint32_t now = millis();

    if (_staConnecting) {
        if (now < _connectAt)
            return;

        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            _staConnecting = false;
            _connected = true;
            _error = false;
            _setMessage("Connected");
            wifiEnabled = true;
            WiFi_LOG("[WiFi] connected, ip=%s\n", WiFi.localIP().toString().c_str());
            if (_apOn && _closeApAfterConnect) {
                _apCloseAt = now + AP_CLOSE_AFTER_CONNECT_MS;
                _setMessage("Connected - setup hotspot closing");
            }
            _closeApAfterConnect = false;
        } else if (now > _connectDeadline) {
            _staConnecting = false;
            _connected = false;
            _error = true;
            switch (st) {
            case WL_NO_SSID_AVAIL:
                _setMessage("Network not found");
                break;
            case WL_CONNECT_FAILED:
                _setMessage("Wrong password or connection refused");
                break;
            case WL_CONNECTION_LOST:
                _setMessage("Connection lost");
                break;
            default:
                _setMessage("Connection timed out");
                break;
            }
            WiFi.disconnect(false, false);
            printf("[WiFi] connect failed: %s\n", _message.c_str());
        }
        return;
    }

    if (_radioOn && !_connected && WiFi.status() == WL_CONNECTED) {
        /* The ESP-IDF station can come back on its own; keep the UI truthful. */
        _connected = true;
        _error = false;
        _setMessage("Connected");
    }

    if (_connected && WiFi.status() != WL_CONNECTED) {
        _connected = false;
        _setMessage("Disconnected");
        printf("[WiFi] link lost\n");
    }

    if (_apCloseAt && now >= _apCloseAt) {
        _apCloseAt = 0;
        _stopPortal();
        _setMessage("Connected");
    }
}

void ConfigWiFi::_applyMode()
{
    wifi_mode_t want;
    if (_apOn)
        want = _radioOn ? WIFI_AP_STA : WIFI_AP;
    else if (_radioOn)
        want = WIFI_STA;
    else
        want = WIFI_OFF;

    if (WiFi.getMode() != want)
        WiFi.mode(want);
}

void ConfigWiFi::_startPortal()
{
    if (_apOn && _serverStarted)
        return;

    _radioOn = true;
    wifiEnabled = true;
    OnAP = true;
    _apOn = true;
    _error = false;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(_apIp, _apIp, IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        printf("[WiFi] softAP start failed\n");
        _apOn = false;
        OnAP = false;
        if (!_connected && !_staConnecting) {
            _radioOn = false;
            wifiEnabled = false;
        }
        _applyMode();
        _setMessage("Failed to start setup hotspot");
        return;
    }

    char token[9];
    snprintf(token, sizeof(token), "%08lx", (unsigned long)esp_random());
    _token = token;

    _dns.start(53, "*", _apIp);
    if (!_serverStarted) {
        _server.begin();
        _serverStarted = true;
    }
    _apCloseAt = 0;
    _closeApAfterConnect = false;
    _setMessage("Setup hotspot ready");
    WiFi_LOG("[WiFi] portal '%s' at %s\n", AP_SSID, _apIp.toString().c_str());
}

void ConfigWiFi::_stopPortal()
{
    if (!_apOn && !_serverStarted)
        return;

    if (_serverStarted) {
        _server.close();
        _serverStarted = false;
    }
    _dns.stop();
    WiFi.softAPdisconnect(true);
    _apOn = false;
    OnAP = false;
    _apCloseAt = 0;
    _closeApAfterConnect = false;

    /* The portal itself is a Wi-Fi function. Without an active or pending STA
     * link there is nothing left to keep the radio on for. */
    if (_connected || _staConnecting) {
        _radioOn = true;
        wifiEnabled = true;
        _applyMode();
    } else {
        _radioOn = false;
        wifiEnabled = false;
        _applyMode();
        _setMessage("Wi-Fi off");
    }
    printf("[WiFi] portal stopped\n");
}

void ConfigWiFi::shutdown()
{
    _staConnecting = false;
    _connected = false;
    _apCloseAt = 0;
    if (_serverStarted) {
        _server.close();
        _serverStarted = false;
    }
    _dns.stop();
    sntp_stop();
    WiFi.disconnect(true, false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _apOn = false;
    _radioOn = false;
    OnAP = false;
    wifiEnabled = false;
    _ntpState = NtpUiState::Idle;
    _setMessage("Wi-Fi off");
}

/* ------------------------------------------------------------------------- */
/* Status getters                                                             */
/* ------------------------------------------------------------------------- */

WifiUiState ConfigWiFi::state() const
{
    if (_staConnecting)
        return _apOn ? WifiUiState::ApConnecting : WifiUiState::Connecting;
    if (_connected)
        return _apOn ? WifiUiState::ApConnected : WifiUiState::Connected;
    /* A failed STA attempt is reported even while the portal stays up, so the
     * phone page can show the failure instead of polling until it times out. */
    if (_error)
        return WifiUiState::Error;
    if (_apOn)
        return WifiUiState::ApMode;
    if (_radioOn)
        return WifiUiState::Starting;
    return WifiUiState::Off;
}

const char *ConfigWiFi::stateText() const
{
    switch (state()) {
    case WifiUiState::Off: return "Off";
    case WifiUiState::Starting: return "Starting";
    case WifiUiState::Connecting: return "Connecting";
    case WifiUiState::Connected: return "Connected";
    case WifiUiState::ApMode: return "AP Mode";
    case WifiUiState::ApConnecting: return "AP + Connecting";
    case WifiUiState::ApConnected: return "AP + Connected";
    case WifiUiState::Error: return "Error";
    }
    return "Off";
}

const char *ConfigWiFi::WiFiMac()
{
    return _macCache.c_str();
}

String ConfigWiFi::currentSSID() const
{
    if (WiFi.status() == WL_CONNECTED)
        return WiFi.SSID();
    return String(_apOn ? AP_SSID : "");
}

String ConfigWiFi::LocalIP()
{
    if (WiFi.status() == WL_CONNECTED)
        return WiFi.localIP().toString();
    return String("--");
}

String ConfigWiFi::GateWay()
{
    if (WiFi.status() == WL_CONNECTED)
        return WiFi.gatewayIP().toString();
    return String("--");
}

String ConfigWiFi::SubnetMask()
{
    if (WiFi.status() == WL_CONNECTED)
        return WiFi.subnetMask().toString();
    return String("--");
}

String ConfigWiFi::DnsIP()
{
    if (WiFi.status() != WL_CONNECTED)
        return String("--");
    IPAddress dns = WiFi.dnsIP(0);
    if (dns == IPAddress(0, 0, 0, 0))
        return String("--");
    return dns.toString();
}

String ConfigWiFi::GetIPv6()
{
    if (WiFi.status() == WL_CONNECTED) {
        String v6 = WiFi.localIPv6().toString();
        if (v6.length() > 0 && v6 != "::")
            return v6;
    }
    return String("--");
}

String ConfigWiFi::APIP()
{
    if (!_apOn)
        return String("--");
    return _apIp.toString();
}

uint8_t ConfigWiFi::apClients() const
{
    /* Cached by update(); safe to call from the LVGL task. */
    return _apOn ? _apClients : 0;
}

bool ConfigWiFi::enableIPv6()
{
    return WiFi.enableIpV6();
}

/* ------------------------------------------------------------------------- */
/* Scanning                                                                   */
/* ------------------------------------------------------------------------- */

void ConfigWiFi::_updateScan()
{
    const uint32_t now = millis();

    if (!_scanRunning) {
        if (!_scanPending)
            return;

        /* A failed start is retried after a short pause instead of hammering
         * esp_wifi_scan_start, which returns ESP_ERR_WIFI_STATE while a scan
         * is still finishing in the driver. */
        if ((int32_t)(now - _scanRetryAt) < 0)
            return;

        if (!_radioOn) {
            _scanPending = false;
            _setScanError("Wi-Fi is off");
            return;
        }

        /* esp_wifi_scan_start can reject the request while the station is
         * associating. Wait for the connection state machine to settle rather
         * than reporting a failure the user cannot act on. */
        if (_staConnecting) {
            if (now - _scanRequestedAt < 20000)
                return;
            _scanPending = false;
            _setScanError("Wi-Fi is still connecting");
            printf("[WiFi] scan deferred: station still connecting\n");
            return;
        }

        _scanPending = false;
        _setScanError("");
        _scanStartMs = now;

        /* The radio must actually be started. A stale WIFI_OFF would make
         * esp_wifi_scan_start fail with ESP_ERR_WIFI_STATE. */
        if (WiFi.getMode() == WIFI_OFF) {
            printf("[WiFi] scan: mode was OFF, re-applying %s\n",
                   _apOn ? "AP+STA" : "STA");
            _applyMode();
        }

        const wifi_mode_t mode = WiFi.getMode();
        const wl_status_t st = WiFi.status();
        const int16_t rc = WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHANNEL);

        printf("[WiFi] scan start: mode=%d status=%d rc=%d\n",
               (int)mode, (int)st, (int)rc);

        if (rc == WIFI_SCAN_RUNNING) {
            _scanRunning = true;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Scan failed to start (rc=%d mode=%d status=%d)",
                     (int)rc, (int)mode, (int)st);
            _setScanError(buf);
            printf("[WiFi] %s\n", buf);
            _scanRetryAt = now + SCAN_RETRY_DELAY_MS;
        }
        return;
    }

    const int16_t n = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING) {
        if (now - _scanStartMs > SCAN_TIMEOUT_MS) {
            _scanRunning = false;
            char buf[64];
            snprintf(buf, sizeof(buf), "Scan timed out after %lus (mode=%d status=%d)",
                     (unsigned long)((now - _scanStartMs) / 1000),
                     (int)WiFi.getMode(), (int)WiFi.status());
            _setScanError(buf);
            printf("[WiFi] %s\n", buf);
            /* Leave driver cleanup to a later request: deleting while the
             * driver is still scanning makes the next start fail. */
            _scanRetryAt = now + SCAN_RETRY_DELAY_MS;
        }
        return;
    }

    _scanRunning = false;

    if (n >= 0) {
        std::vector<ScanEntry> list;
        list.reserve((size_t)n);
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0)
                continue;
            ScanEntry e;
            e.ssid = ssid.c_str();
            e.rssi = WiFi.RSSI(i);
            e.secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            list.push_back(e);
            if (list.size() >= 24)
                break;
        }
        WiFi.scanDelete();
        {
            LockGuard guard(_scanLock);
            _scanList.swap(list);
            _scanGeneration++;
        }
        printf("[WiFi] scan complete: %d network(s) (mode=%d)\n", (int)n, (int)WiFi.getMode());
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Scan failed (rc=%d mode=%d status=%d elapsed=%lums)",
                 (int)n, (int)WiFi.getMode(), (int)WiFi.status(),
                 (unsigned long)(now - _scanStartMs));
        _setScanError(buf);
        printf("[WiFi] %s\n", buf);
        WiFi.scanDelete();
        _scanRetryAt = now + SCAN_RETRY_DELAY_MS;
    }
}

size_t ConfigWiFi::scanCount() const
{
    LockGuard guard(_scanLock);
    return _scanList.size();
}

bool ConfigWiFi::scanEntry(size_t index, String &ssid, int32_t &rssi, bool &secure) const
{
    LockGuard guard(_scanLock);
    if (index >= _scanList.size())
        return false;
    ssid = _scanList[index].ssid.c_str();
    rssi = _scanList[index].rssi;
    secure = _scanList[index].secure;
    return true;
}

/* ------------------------------------------------------------------------- */
/* NTP (non-blocking)                                                         */
/* ------------------------------------------------------------------------- */

const char *ConfigWiFi::ntpServerName(uint8_t index)
{
    return index < NTP_SERVER_COUNT ? NTP_SERVERS[index] : NTP_SERVERS[0];
}

uint8_t ConfigWiFi::ntpServerCount() const
{
    return NTP_SERVER_COUNT;
}

const char *ConfigWiFi::ntpServer() const
{
    return ntpServerName(_ntpIndex);
}

const char *ConfigWiFi::ntpStateText() const
{
    switch (_ntpState) {
    case NtpUiState::Disabled: return "Disabled";
    case NtpUiState::Idle: return "Idle";
    case NtpUiState::Syncing: return "Syncing...";
    case NtpUiState::Ok: return "Synced";
    case NtpUiState::Failed: return "Failed";
    }
    return "Idle";
}

void ConfigWiFi::_updateNtp()
{
    const uint32_t now = millis();

    bool manual;
    {
        LockGuard guard(_lock);
        manual = _ntpManual;
        _ntpManual = false;
    }

    /* Poll an in-flight sync first: a manual sync must keep running even when
     * automatic sync is disabled, and disabling auto must not cancel it. */
    if (_ntpState == NtpUiState::Syncing) {
        struct tm tm_now;
        if (getLocalTime(&tm_now, 0) && (tm_now.tm_year + 1900) >= 2024) {
            _writeRtcFromSystemTime();
            _ntpState = NtpUiState::Ok;
            _ntpLastSync = now;
            _setNtpError("");
            sntp_stop();
            printf("[NTP] synced\n");
        } else if (now - _ntpStart > NTP_SYNC_TIMEOUT_MS) {
            _ntpState = NtpUiState::Failed;
            _setNtpError("Server did not respond");
            _ntpRetryAt = now + NTP_RETRY_DELAY_MS;
            sntp_stop();
            printf("[NTP] sync timeout\n");
        }
        return;
    }

    if (!AutoTime && !manual) {
        /* Keep the last Ok/Failed result visible; only an idle state becomes
         * "Disabled". */
        if (_ntpState == NtpUiState::Idle)
            _ntpState = NtpUiState::Disabled;
        return;
    }

    if (!_connected) {
        if (manual) {
            _ntpState = NtpUiState::Failed;
            _setNtpError("No network");
        } else if (_ntpState == NtpUiState::Failed && !_ntpHasError) {
            _setNtpError("No network");
        }
        return;
    }

    bool shouldStart = manual;
    if (!shouldStart && AutoTime) {
        if (_ntpState == NtpUiState::Idle)
            shouldStart = true;
        else if (_ntpState == NtpUiState::Failed && now >= _ntpRetryAt)
            shouldStart = true;
        else if (_ntpState == NtpUiState::Ok && now - _ntpLastSync >= NTP_PERIODIC_MS)
            shouldStart = true;
    }

    if (shouldStart)
        _startNtp();
}

void ConfigWiFi::_startNtp()
{
    if (!_connected)
        return;

    /* configTzTime starts SNTP and sets the timezone. It does not block.
     * The Arduino core 2.0.x signature returns void; success is judged by
     * the poll in _updateNtp(). */
    configTzTime("CST-8", ntpServerName(_ntpIndex));
    _ntpState = NtpUiState::Syncing;
    _ntpStart = millis();
    _setNtpError("");
    printf("[NTP] syncing with %s\n", ntpServerName(_ntpIndex));
}

void ConfigWiFi::_writeRtcFromSystemTime()
{
    if (_rtc == nullptr)
        return;

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    I2C_BM8563_TimeTypeDef t;
    t.hours = (int8_t)tm_now.tm_hour;
    t.minutes = (int8_t)tm_now.tm_min;
    t.seconds = (int8_t)tm_now.tm_sec;
    _rtc->setTime(&t);

    I2C_BM8563_DateTypeDef d;
    d.weekDay = (int8_t)tm_now.tm_wday;
    d.month = (int8_t)(tm_now.tm_mon + 1);
    d.date = (int8_t)tm_now.tm_mday;
    d.year = (int16_t)(tm_now.tm_year + 1900);
    _rtc->setDate(&d);
}

/* ------------------------------------------------------------------------- */
/* HTTP portal                                                                */
/* ------------------------------------------------------------------------- */

bool ConfigWiFi::_checkToken()
{
    if (_token.length() == 0)
        return false;
    if (!_server.hasArg("token"))
        return false;
    return _server.arg("token") == _token;
}

String ConfigWiFi::_jsonEscape(const String &in)
{
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ((uint8_t)c < 0x20)
                out += ' ';
            else
                out += c;
            break;
        }
    }
    return out;
}

void ConfigWiFi::_handleRoot()
{
    static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Chappie 2 · Wi-Fi</title><style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:#0a0c0f;color:#f2f4f7;font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",Arial,"PingFang SC","Microsoft YaHei",sans-serif}
.wrap{max-width:460px;margin:0 auto;padding:22px 18px 40px}
h1{font-size:20px;letter-spacing:.5px;margin:0 0 2px}
.sub{color:#7a8089;font-size:13px;margin-bottom:18px}
.card{background:#121519;border:1px solid #1e2329;border-radius:14px;padding:16px;margin-bottom:14px}
button{font:inherit;border:0;border-radius:10px;padding:12px 16px;background:#1b2027;color:#f2f4f7;width:100%;cursor:pointer}
button.primary{background:#18c7a8;color:#04120f;font-weight:600}
button:disabled{opacity:.5}
input{font:inherit;width:100%;padding:12px;border-radius:10px;border:1px solid #262c34;background:#0d1013;color:#f2f4f7;outline:none}
.row{display:flex;align-items:center;gap:12px;padding:12px 4px;border-bottom:1px solid #1a1f25;cursor:pointer}
.row:last-child{border-bottom:0}
.ssid{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.meta{color:#7a8089;font-size:12px}
.badge{font-size:11px;color:#8a919b;border:1px solid #262c34;border-radius:6px;padding:2px 6px}
.bar{height:3px;background:#1b2027;border-radius:2px;overflow:hidden;margin-top:10px}
.bar i{display:block;height:100%;width:35%;background:#ffb02e;animation:m 1.1s linear infinite}
@keyframes m{0%{transform:translateX(-100%)}100%{transform:translateX(320%)}}
.hide{display:none!important}
.ok{color:#18c7a8}.err{color:#e5484d}
.status{font-size:14px;margin-top:10px;min-height:20px}
.tick{width:52px;height:52px;border-radius:50%;border:2px solid #18c7a8;display:flex;align-items:center;justify-content:center;margin:6px auto 10px;font-size:26px;color:#18c7a8}
.small{font-size:12px;color:#7a8089}
</style></head><body><div class="wrap">
<h1>Chappie 2</h1><div class="sub">设备 Wi-Fi 配网</div>
<div class="card" id="c-scan">
  <div style="font-weight:600;margin-bottom:8px">选择网络</div>
  <div class="small" style="margin-bottom:12px">设备仅支持 2.4 GHz Wi-Fi。</div>
  <button class="primary" id="b-scan">扫描附近网络</button>
  <div class="bar hide" id="bar"><i></i></div>
  <div id="list"></div>
  <div class="status" id="scan-msg"></div>
</div>
<div class="card hide" id="c-pass">
  <div style="font-weight:600" id="p-title">输入密码</div>
  <div class="small" style="margin:6px 0 12px" id="p-sub"></div>
  <input id="pass" type="password" autocomplete="new-password" placeholder="Wi-Fi 密码">
  <label style="display:flex;align-items:center;gap:8px;margin:10px 2px;font-size:13px;color:#aab1ba">
    <input type="checkbox" id="show" style="width:auto;padding:0"> 显示密码
  </label>
  <button class="primary" id="b-connect">连接</button>
  <button id="b-back" style="margin-top:8px">返回</button>
  <div class="status" id="pass-msg"></div>
</div>
<div class="card hide" id="c-done">
  <div class="tick" id="tick">✓</div>
  <div style="text-align:center;font-weight:600" id="done-title"></div>
  <div class="status" style="text-align:center;color:#7a8089" id="done-msg"></div>
</div>
<div class="small" style="text-align:center;margin-top:18px">AP: Chappie-AP · http://192.168.4.1</div>
</div>
<script>
const TOKEN="%TOKEN%";
const $=id=>document.getElementById(id);
let selected=null,busy=false;
function api(path,opts){return fetch(path+(path.includes('?')?'&':'?')+'token='+encodeURIComponent(TOKEN),opts).then(r=>r.json());}
function show(id){for(const c of ['c-scan','c-pass','c-done'])$(c).classList.toggle('hide',c!==id);}
function render(nets){
  const l=$('list');l.textContent='';
  if(!nets.length){$('scan-msg').textContent='未发现网络，请重试。';return;}
  nets.forEach(n=>{
    const d=document.createElement('div');d.className='row';
    const s=document.createElement('div');s.className='ssid';s.textContent=n.ssid||'(隐藏网络)';
    const b=document.createElement('span');b.className='badge';b.textContent=n.secure?'加密':'开放';
    const r=document.createElement('span');r.className='meta';r.textContent=n.rssi+' dBm';
    d.append(s,b,r);d.onclick=()=>pick(n);l.append(d);
  });
}
function pick(n){selected=n;$('p-title').textContent='连接到 '+n.ssid;$('p-sub').textContent=n.secure?'请输入 Wi-Fi 密码（至少 8 位）':'开放网络，无需密码';$('pass').value='';$('pass-msg').textContent='';show('c-pass');$('pass').focus();}
async function scan(){
  if(busy)return;busy=true;$('b-scan').disabled=true;$('bar').classList.remove('hide');$('scan-msg').textContent='正在扫描…';$('list').textContent='';
  try{
    await api('/scan');
    for(let i=0;i<40;i++){
      await new Promise(r=>setTimeout(r,700));
      const st=await api('/scan_results');
      if(!st.scanning){render(st.networks||[]);$('scan-msg').textContent=st.error?st.error:'';break;}
    }
  }catch(e){$('scan-msg').textContent='扫描失败，请重试。';}
  $('bar').classList.add('hide');$('b-scan').disabled=false;busy=false;
}
async function connect(){
  if(!selected||busy)return;
  const pass=$('pass').value;
  if(selected.secure&&pass.length>0&&pass.length<8){$('pass-msg').textContent='密码至少 8 位。';return;}
  if(pass.length>63){$('pass-msg').textContent='密码过长。';return;}
  busy=true;$('b-connect').disabled=true;$('pass-msg').textContent='正在提交…';
  try{
    const body=new URLSearchParams({token:TOKEN,ssid:selected.ssid,password:pass});
    const r=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const st=await r.json();
    if(!st.ok){$('pass-msg').textContent=st.message||'提交失败';busy=false;$('b-connect').disabled=false;return;}
    $('pass-msg').textContent='已保存，正在切换网络…';
    poll();
  }catch(e){$('pass-msg').textContent='提交失败，请重试。';busy=false;$('b-connect').disabled=false;}
}
async function poll(){
  let last='';
  for(let i=0;i<45;i++){
    await new Promise(r=>setTimeout(r,900));
    try{
      const st=await api('/status');
      last=st.state;
      if(st.state==='connected'||st.state==='ap_connected'){
        show('c-done');$('done-title').textContent='已连接 '+(st.ssid||selected.ssid);
        $('done-msg').textContent='设备已切换到 Wi-Fi，配网热点即将关闭。';
        busy=false;return;
      }
      if(st.state==='error'){
        show('c-pass');$('pass-msg').textContent=(st.message||'连接失败')+'，请检查密码后重试。';
        busy=false;$('b-connect').disabled=false;return;
      }
      $('pass-msg').textContent='连接中… '+(st.message||'');
    }catch(e){
      show('c-done');$('done-title').textContent='设备正在切换网络';
      $('done-msg').textContent='手机已断开配网热点，请稍后在设备上确认连接结果。';
      busy=false;return;
    }
  }
  $('pass-msg').textContent='连接超时，请重试。';busy=false;$('b-connect').disabled=false;
}
$('b-scan').onclick=scan;
$('b-connect').onclick=connect;
$('b-back').onclick=()=>{show('c-scan');};
$('show').onchange=e=>{$('pass').type=e.target.checked?'text':'password';};
$('pass').addEventListener('keydown',e=>{if(e.key==='Enter')connect();});
scan();
</script></body></html>)HTML";

    if (!_apOn) {
        _server.send(503, "text/plain", "Portal is not active");
        return;
    }
    String page = FPSTR(PAGE);
    page.replace("%TOKEN%", _token);
    _server.send(200, "text/html; charset=utf-8", page);
}

void ConfigWiFi::_handleScanStart()
{
    if (!_checkToken()) {
        _server.send(403, "application/json", "{\"ok\":false,\"message\":\"invalid token\"}");
        return;
    }
    /* The page polls /scan_results, so an already running scan is not an
     * error: tell the client to keep polling instead of queueing a second
     * scan behind the active one. */
    if (scanning()) {
        _server.send(200, "application/json", "{\"ok\":true,\"scanning\":true,\"already\":true}");
        return;
    }
    requestScan();
    _server.send(200, "application/json", "{\"ok\":true,\"scanning\":true}");
}

void ConfigWiFi::_handleScanResults()
{
    if (!_checkToken()) {
        _server.send(403, "application/json", "{\"ok\":false,\"message\":\"invalid token\"}");
        return;
    }

    String json;
    json.reserve(512);
    json += "{\"ok\":true,\"scanning\":";
    json += scanning() ? "true" : "false";
    json += ",\"error\":\"";
    json += _jsonEscape(scanErrorText());
    json += "\",\"networks\":[";

    LockGuard guard(_scanLock);
    for (size_t i = 0; i < _scanList.size(); i++) {
        if (i)
            json += ',';
        json += "{\"ssid\":\"";
        json += _jsonEscape(String(_scanList[i].ssid.c_str()));
        json += "\",\"rssi\":";
        json += String(_scanList[i].rssi);
        json += ",\"secure\":";
        json += _scanList[i].secure ? "true" : "false";
        json += '}';
    }
    json += "]}";
    _server.send(200, "application/json", json);
}

void ConfigWiFi::_handleConnect()
{
    if (!_checkToken()) {
        _server.send(403, "application/json", "{\"ok\":false,\"message\":\"invalid token\"}");
        return;
    }
    if (!_apOn) {
        _server.send(409, "application/json", "{\"ok\":false,\"message\":\"portal is not active\"}");
        return;
    }

    String ssid = _server.arg("ssid");
    String pass = _server.arg("password");

    bool bad = false;
    if (ssid.length() == 0 || ssid.length() > 32)
        bad = true;
    for (size_t i = 0; i < ssid.length() && !bad; i++) {
        uint8_t c = (uint8_t)ssid[i];
        if (c < 0x20 || c == 0x7F)
            bad = true;
    }
    if (pass.length() > 63)
        bad = true;
    for (size_t i = 0; i < pass.length() && !bad; i++) {
        uint8_t c = (uint8_t)pass[i];
        if (c < 0x20 || c == 0x7F)
            bad = true;
    }

    if (bad) {
        _server.send(400, "application/json", "{\"ok\":false,\"message\":\"invalid SSID or password\"}");
        return;
    }

    /* Save first, answer second, switch on a later update(). This guarantees
     * the phone receives the response before the AP/STA transition. */
    _saveCredentials(ssid, pass);
    requestConnect(ssid, pass, true);

    _server.send(200, "application/json", "{\"ok\":true,\"state\":\"connecting\"}");
}

void ConfigWiFi::_handleStatus()
{
    if (!_checkToken()) {
        _server.send(403, "application/json", "{\"ok\":false,\"message\":\"invalid token\"}");
        return;
    }

    const char *st = "off";
    switch (state()) {
    case WifiUiState::Off: st = "off"; break;
    case WifiUiState::Starting: st = "starting"; break;
    case WifiUiState::Connecting: st = "connecting"; break;
    case WifiUiState::Connected: st = "connected"; break;
    case WifiUiState::ApMode: st = "ap"; break;
    case WifiUiState::ApConnecting: st = "ap_connecting"; break;
    case WifiUiState::ApConnected: st = "ap_connected"; break;
    case WifiUiState::Error: st = "error"; break;
    }

    String json;
    json.reserve(256);
    json += "{\"ok\":true,\"state\":\"";
    json += st;
    json += "\",\"ssid\":\"";
    json += _jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(""));
    json += "\",\"ip\":\"";
    json += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
    json += "\",\"message\":\"";
    json += _jsonEscape(String(_message.c_str()));
    json += "\",\"ap\":";
    json += _apOn ? "true" : "false";
    json += '}';
    _server.send(200, "application/json", json);
}
