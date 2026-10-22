/**
 * @file ConfigBLE.h
 * @brief BLE Nordic UART Service bridge - using standard ESP32 BLE API
 * Uses the framework's built-in BLE library (<BLEDevice.h>) to avoid conflicts.
 *
 * The UI posts enable/disable requests; ConfigBLE::update() applies them in the
 * Arduino loop task so the LVGL task never calls into the BT stack.
 */
#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <BLE2902.h>

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_RX_CAP 2048

enum class BleUiState : uint8_t {
    Off = 0,
    Starting,
    Advertising,
    Connected,
    Error,
};

class ConfigBLE {
public:
    ConfigBLE();
    ~ConfigBLE();

    void Init(const char* deviceName = nullptr);

    /** Applied immediately. Prefer request*() when called from the UI task. */
    void enable();
    void disable();

    /** Thread-safe requests processed by update() in the main loop task. */
    void requestEnable();
    void requestDisable();
    void requestToggle();
    void update();

    bool isEnabled() const { return _enabled; }
    bool isConnected() const { return _connected; }
    bool isSecure() const { return _secure; }
    uint32_t getPasskey() const { return _passkey; }

    BleUiState state() const;
    const char* stateText() const;
    const char* errorText() const { return _errorText; }

    size_t available();
    int read();
    size_t write(const uint8_t* data, size_t len);
    size_t write(const char* str);
    void clearBonds();

    /* Bond (paired device) records actually stored by the controller. */
    int getBondCount();
    bool getBondAddress(int index, String& out);
    bool removeBond(int index);

    String getDeviceName() const { return _deviceName; }
    String getConnectedAddress();   /* copy under lock (written by the BT task) */

private:
    friend class BLE_ServerCallbacks;
    friend class BLE_RxCallbacks;
    friend class BLE_SecCallbacks;

    volatile bool _enabled;
    volatile bool _connected;
    volatile bool _secure;
    volatile bool _advertising;
    volatile bool _starting;
    volatile bool _wantEnable;
    volatile bool _wantDisable;
    SemaphoreHandle_t _stateMutex;
    volatile uint32_t _passkey;
    uint16_t _mtu;
    String _deviceName;
    String _connectedAddr;
    char _errorText[48];

    /* Bond count is sampled in update() (main task) so the LVGL task never
     * calls into the BT stack while refreshing the UI. */
    volatile int _bondCount = 0;
    uint32_t _bondCheckAt = 0;

    /* Next advertising supervision attempt (see update()). */
    uint32_t _advRetryAt = 0;

    uint8_t _rxBuf[BLE_RX_CAP];
    volatile size_t _rxHead;
    volatile size_t _rxTail;

    void _rxPush(const uint8_t* data, size_t len);
    void _startAdvertising();
    void _stopAdvertising();

    BLEServer* _server;
    BLECharacteristic* _txChar;
    BLECharacteristic* _rxChar;
};

#define BLE_LOG(format, args...) printf("[BLE] " format, ##args)
