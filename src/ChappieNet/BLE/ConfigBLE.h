/**
 * @file ConfigBLE.h
 * @brief BLE Nordic UART Service bridge - using standard ESP32 BLE API
 * Uses the framework's built-in BLE library (<BLEDevice.h>) to avoid conflicts.
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

class ConfigBLE {
public:
    ConfigBLE();
    ~ConfigBLE();

    void Init(const char* deviceName = nullptr);
    void enable();
    void disable();
    bool isEnabled() const { return _enabled; }
    bool isConnected() const { return _connected; }
    bool isSecure() const { return _secure; }
    uint32_t getPasskey() const { return _passkey; }

    size_t available();
    int read();
    size_t write(const uint8_t* data, size_t len);
    size_t write(const char* str);
    void clearBonds();

    String getDeviceName() const { return _deviceName; }
    String getConnectedAddress() const { return _connectedAddr; }

private:
    friend class BLE_ServerCallbacks;
    friend class BLE_RxCallbacks;
    friend class BLE_SecCallbacks;

    bool _enabled;
    volatile bool _connected;
    volatile bool _secure;
    bool _advertising;
    volatile uint32_t _passkey;
    uint16_t _mtu;
    String _deviceName;
    String _connectedAddr;

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
