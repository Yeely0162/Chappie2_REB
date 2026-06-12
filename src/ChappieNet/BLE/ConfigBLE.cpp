/**
 * @file ConfigBLE.cpp
 * @brief BLE Nordic UART Service implementation using standard ESP32 BLE
 */
#include "ConfigBLE.h"
#include <esp_mac.h>

// ========== Server Callbacks ==========

class BLE_ServerCallbacks : public BLEServerCallbacks {
public:
    BLE_ServerCallbacks(ConfigBLE* parent) : _parent(parent) {}

    void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* param) override {
        _parent->_connected = true;
        if (param) {
            _parent->_connectedAddr = "";
            for (int i = 0; i < 6; i++) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X", param->connect.remote_bda[i]);
                _parent->_connectedAddr += buf;
                if (i < 5) _parent->_connectedAddr += ":";
            }
        }
        BLE_LOG("Connected: %s\n", _parent->_connectedAddr.c_str());
    }

    void onDisconnect(BLEServer* s) override {
        _parent->_connected = false;
        _parent->_secure = false;
        _parent->_passkey = 0;
        _parent->_mtu = 23;
        _parent->_connectedAddr = "";
        BLE_LOG("Disconnected\n");
        if (_parent->_enabled) {
            _parent->_startAdvertising();
        }
    }

    void onMtuChanged(BLEServer* s, esp_ble_gatts_cb_param_t* param) override {
        if (param) {
            _parent->_mtu = param->mtu.mtu;
            BLE_LOG("MTU: %u\n", _parent->_mtu);
        }
    }

private:
    ConfigBLE* _parent;
};

// ========== RX Characteristic Callbacks ==========

class BLE_RxCallbacks : public BLECharacteristicCallbacks {
public:
    BLE_RxCallbacks(ConfigBLE* parent) : _parent(parent) {}

    void onWrite(BLECharacteristic* c) override {
        std::string value = c->getValue();
        if (value.length() > 0) {
            _parent->_rxPush((const uint8_t*)value.c_str(), value.length());
        }
    }

private:
    ConfigBLE* _parent;
};

// ========== Security Callbacks ==========

class BLE_SecCallbacks : public BLESecurityCallbacks {
public:
    BLE_SecCallbacks(ConfigBLE* parent) : _parent(parent) {}

    uint32_t onPassKeyRequest() override { return 0; }
    bool onConfirmPIN(uint32_t) override { return false; }
    bool onSecurityRequest() override { return true; }

    void onPassKeyNotify(uint32_t pk) override {
        _parent->_passkey = pk;
        BLE_LOG("Passkey: %06lu\n", (unsigned long)pk);
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        // Already handled in server callbacks
    }

private:
    ConfigBLE* _parent;
};

// ========== ConfigBLE Implementation ==========

ConfigBLE::ConfigBLE()
    : _enabled(false), _connected(false), _secure(false), _advertising(false),
      _passkey(0), _mtu(23), _rxHead(0), _rxTail(0),
      _server(nullptr), _txChar(nullptr), _rxChar(nullptr) {}

ConfigBLE::~ConfigBLE() { disable(); }

void ConfigBLE::Init(const char* deviceName) {
    if (deviceName) {
        _deviceName = deviceName;
    } else {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        char name[24];
        snprintf(name, sizeof(name), "Chappie-%02X%02X", mac[4], mac[5]);
        _deviceName = name;
    }
    BLE_LOG("Init: %s\n", _deviceName.c_str());
}

void ConfigBLE::enable() {
    if (_enabled) return;

    BLEDevice::init(_deviceName.c_str());
    BLEDevice::setMTU(517);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(new BLE_SecCallbacks(this));

    _server = BLEDevice::createServer();
    _server->setCallbacks(new BLE_ServerCallbacks(this));

    BLEService* service = _server->createService(NUS_SERVICE_UUID);

    // TX characteristic (Notify)
    _txChar = service->createCharacteristic(
        NUS_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _txChar->addDescriptor(new BLE2902());

    // RX characteristic (Write)
    _rxChar = service->createCharacteristic(
        NUS_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    _rxChar->setCallbacks(new BLE_RxCallbacks(this));

    service->start();

    // Security
    BLESecurity* sec = new BLESecurity();
    sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    sec->setCapability(ESP_IO_CAP_OUT);
    sec->setKeySize(16);
    sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    _enabled = true;
    _startAdvertising();
    BLE_LOG("BLE enabled\n");
}

void ConfigBLE::disable() {
    if (!_enabled) return;

    _stopAdvertising();
    if (_server) {
        _server->getConnectedCount();
        BLEDevice::deinit(true);
    }

    _enabled = false;
    _connected = false;
    _secure = false;
    _passkey = 0;
    _server = nullptr;
    _txChar = nullptr;
    _rxChar = nullptr;
    _rxHead = 0;
    _rxTail = 0;
    BLE_LOG("BLE disabled\n");
}

size_t ConfigBLE::available() {
    return (_rxHead + BLE_RX_CAP - _rxTail) % BLE_RX_CAP;
}

int ConfigBLE::read() {
    if (_rxHead == _rxTail) return -1;
    int b = _rxBuf[_rxTail];
    _rxTail = (_rxTail + 1) % BLE_RX_CAP;
    return b;
}

size_t ConfigBLE::write(const uint8_t* data, size_t len) {
    if (!_connected || !_txChar) return 0;

    size_t chunk = (_mtu > 3) ? _mtu - 3 : 20;
    if (chunk > 180) chunk = 180;

    size_t sent = 0;
    while (sent < len) {
        size_t n = len - sent;
        if (n > chunk) n = chunk;
        _txChar->setValue((uint8_t*)(data + sent), n);
        _txChar->notify();
        sent += n;
        delay(4);
    }
    return sent;
}

size_t ConfigBLE::write(const char* str) {
    return write((const uint8_t*)str, strlen(str));
}

void ConfigBLE::clearBonds() {
    int n = esp_ble_get_bond_device_num();
    if (n <= 0) return;
    esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(n * sizeof(esp_ble_bond_dev_t));
    if (!list) return;
    esp_ble_get_bond_device_list(&n, list);
    for (int i = 0; i < n; i++) esp_ble_remove_bond_device(list[i].bd_addr);
    free(list);
    BLE_LOG("Cleared %d bond(s)\n", n);
}

void ConfigBLE::_rxPush(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        size_t next = (_rxHead + 1) % BLE_RX_CAP;
        if (next == _rxTail) return;
        _rxBuf[_rxHead] = data[i];
        _rxHead = next;
    }
}

void ConfigBLE::_startAdvertising() {
    if (_advertising) return;

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    _advertising = true;
    BLE_LOG("Advertising as '%s'\n", _deviceName.c_str());
}

void ConfigBLE::_stopAdvertising() {
    if (!_advertising) return;
    BLEDevice::stopAdvertising();
    _advertising = false;
}
