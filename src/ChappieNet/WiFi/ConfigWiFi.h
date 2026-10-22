/**
 * @file ConfigWiFi.h
 * @brief Wi-Fi / AP-STA state machine, phone provisioning portal and non-blocking NTP.
 *
 * Design notes
 * ------------
 * - Every Wi-Fi API call is executed from ConfigWiFi::update(), which runs in
 *   the Arduino loop task. LVGL callbacks only post requests, so the LVGL task
 *   never touches the Wi-Fi driver.
 * - The provisioning portal is an AP+STA interface: the HTTP handler saves the
 *   credentials and answers first, then the state machine switches to STA on a
 *   later update(). This removes the old "shows success but needs a manual
 *   off/on" bug.
 * - NTP is polled (getLocalTime with a zero timeout) and never blocks the UI.
 * - No credential value is ever written to the serial log.
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <HTTPClient.h>   /* kept for apps that included ConfigWiFi.h transitively */
#include <vector>
#include "ChappieBsp/Utility/BM8563/I2C_BM8563.h"

class ConfigWiFi;

/**
 * @brief Public Wi-Fi state used by the UI. The individual flags can be true
 *        at the same time (e.g. the config portal is up while STA connects).
 */
enum class WifiUiState : uint8_t {
    Off = 0,
    Starting,
    Connecting,
    Connected,
    ApMode,
    ApConnecting,
    ApConnected,
    Error,
};

enum class NtpUiState : uint8_t {
    Disabled = 0,
    Idle,
    Syncing,
    Ok,
    Failed,
};

class ConfigWiFi
{
public:
    ConfigWiFi();
    ~ConfigWiFi();

    /* Compatibility flags kept for existing callers / UI. */
    volatile bool AutoTime = true;
    volatile bool wifiEnabled = false;   /* STA function enabled */
    volatile bool OnAP = false;          /* provisioning portal active */

    static constexpr const char *AP_SSID = "Chappie-AP";
    static constexpr const char *AP_PASSWORD = "chappie123";   /* WPA2, min 8 chars */

    void Init(I2C_BM8563 *rtc = nullptr);

    /**
     * @brief Run the network state machine. Call from the Arduino loop task
     *        (CHAPPIE::update) only.
     */
    void update();
    void shutdown();

    /* ---------------- requests (safe from any task) ---------------- */
    void enableWiFi();             /* connect with saved credentials, or start portal */
    void disableWiFi();            /* stop STA, AP and radio */
    void APMode();                 /* start the provisioning portal */
    bool APClose();                /* stop the provisioning portal */
    void requestToggle();
    void requestConnect(const String &ssid, const String &password, bool closeApAfterConnect);
    void requestDisconnect();
    void requestForget();
    void requestScan();
    void requestNtpSync();
    void setAutoTime(bool on);
    void setNtpServerIndex(uint8_t index);

    /* Legacy name: previously a blocking sync called from an LVGL timer. */
    void NtpTimeCorr() { requestNtpSync(); }
    /* Legacy loop hook: the state machine is driven by update() now. */
    void WiFiloop() {}

    /* ---------------- state ---------------- */
    WifiUiState state() const;
    const char *stateText() const;
    /* Copies safe to call from the LVGL task. */
    String lastErrorText();
    String configuredSSID();

    bool isWiFiEnabled() const { return _radioOn; }
    bool isConnected() const { return _connected; }
    bool isAPActive() const { return _apOn; }
    bool isBusy() const { return _staConnecting; }
    bool enableIPv6();

    /* Configured credentials / live connection */
    const char *WiFiMac();
    String currentSSID() const;
    String LocalIP();
    String GateWay();
    String SubnetMask();
    String DnsIP();
    String GetIPv6();
    String APIP();
    uint8_t apClients() const;

    /* Scan results (copy under mutex, safe from the UI task) */
    bool scanning() const { return _scanRunning || _scanPending; }
    size_t scanCount() const;
    bool scanEntry(size_t index, String &ssid, int32_t &rssi, bool &secure) const;
    uint32_t scanGeneration() const { return _scanGeneration; }
    String scanErrorText();

    /* NTP */
    NtpUiState ntpState() const { return _ntpState; }
    const char *ntpStateText() const;
    const char *ntpServer() const;
    uint8_t ntpServerIndex() const { return _ntpIndex; }
    uint8_t ntpServerCount() const;
    static const char *ntpServerName(uint8_t index);
    uint32_t ntpLastSyncMs() const { return _ntpLastSync; }
    String ntpLastErrorText();

private:
    /* Pending commands posted by the UI task */
    enum class Cmd : uint8_t {
        None, Enable, Disable, Connect, Disconnect, Forget,
        ApStart, ApStop, Scan, NtpAutoOn, NtpAutoOff, NtpServer
    };

    void _post(Cmd cmd, const String *ssid = nullptr, const String *pass = nullptr, bool closeAp = false);
    void _applyCommand();
    void _doConnect(const String &ssid, const String &pass, bool closeAp);

    void _startPortal();
    void _stopPortal();
    void _applyMode();
    void _loadConfig();
    void _saveCredentials(const String &ssid, const String &pass);

    void _updateSta();
    void _updateScan();
    void _updateNtp();
    void _startNtp();
    void _writeRtcFromSystemTime();

    /* Guarded text updates (the UI task reads copies). */
    void _setMessage(const char *msg);
    void _setNtpError(const char *msg);
    void _setScanError(const char *msg);

    /* HTTP handlers */
    void _handleRoot();
    void _handleScanStart();
    void _handleScanResults();
    void _handleConnect();
    void _handleStatus();
    bool _checkToken();
    static String _jsonEscape(const String &in);

    /* Portal */
    DNSServer _dns;
    WebServer _server{80};
    bool _serverStarted = false;
    String _token;
    IPAddress _apIp{192, 168, 4, 1};

    /* Configuration */
    std::string _ssid;
    std::string _pass;
    std::string _macCache = "None";
    volatile uint8_t _ntpIndex = 0;

    /* State machine */
    volatile bool _radioOn = false;
    volatile bool _apOn = false;
    volatile bool _connected = false;
    volatile bool _staConnecting = false;
    volatile uint8_t _apClients = 0;
    uint32_t _connectStart = 0;
    uint32_t _connectDeadline = 0;
    uint32_t _connectAt = 0;
    volatile bool _error = false;
    uint32_t _apCloseAt = 0;
    bool _closeApAfterConnect = false;
    std::string _message = "Wi-Fi off";

    /* Pending command (guarded by _lock) */
    SemaphoreHandle_t _lock = nullptr;
    Cmd _cmd = Cmd::None;
    std::string _cmdSsid;
    std::string _cmdPass;
    bool _cmdCloseAp = false;
    uint8_t _cmdIndex = 0;

    /* Scan */
    struct ScanEntry { std::string ssid; int32_t rssi = 0; bool secure = false; };
    mutable SemaphoreHandle_t _scanLock = nullptr;
    std::vector<ScanEntry> _scanList;
    volatile bool _scanRunning = false;
    volatile bool _scanPending = false;
    volatile uint32_t _scanGeneration = 1;
    volatile uint32_t _scanRequestedAt = 0;
    uint32_t _scanStartMs = 0;
    uint32_t _scanRetryAt = 0;
    char _scanError[64] = "";

    /* NTP */
    I2C_BM8563 *_rtc = nullptr;
    volatile NtpUiState _ntpState = NtpUiState::Idle;
    bool _ntpManual = false;
    uint32_t _ntpStart = 0;
    volatile uint32_t _ntpLastSync = 0;
    uint32_t _ntpRetryAt = 0;
    char _ntpError[48] = "";
    volatile bool _ntpHasError = false;
};

#define WiFi_LOG(format, args...) printf(format, ##args)
