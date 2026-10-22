/**
 * @file ConfigBLE.cpp
 * @brief BLE Nordic UART Service implementation using standard ESP32 BLE
 */
#include "ConfigBLE.h"
#include <esp_mac.h>
#include <esp_bt.h>
#include <esp_bt_main.h>

// ========== Server Callbacks ==========

class BLE_ServerCallbacks : public BLEServerCallbacks {
public:
    BLE_ServerCallbacks(ConfigBLE* parent) : _parent(parent) {}

    void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* param) override {
        _parent->_connected = true;
        /* The controller stops advertising once a link is established. Clear
         * the flag so onDisconnect() can restart it. */
        _parent->_advertising = false;
        String addr;
        if (param) {
            for (int i = 0; i < 6; i++) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X", param->connect.remote_bda[i]);
                addr += buf;
                if (i < 5) addr += ":";
            }
        }
        if (_parent->_stateMutex)
            xSemaphoreTake(_parent->_stateMutex, portMAX_DELAY);
        _parent->_connectedAddr = addr;
        if (_parent->_stateMutex)
            xSemaphoreGive(_parent->_stateMutex);
        BLE_LOG("Connected: %s\n", addr.c_str());
    }

    void onDisconnect(BLEServer* s) override {
        _parent->_connected = false;
        _parent->_secure = false;
        _parent->_passkey = 0;
        _parent->_mtu = 23;
        if (_parent->_stateMutex)
            xSemaphoreTake(_parent->_stateMutex, portMAX_DELAY);
        _parent->_connectedAddr = "";
        if (_parent->_stateMutex)
            xSemaphoreGive(_parent->_stateMutex);
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
      _starting(false), _wantEnable(false), _wantDisable(false),
      _stateMutex(xSemaphoreCreateMutex()),
      _passkey(0), _mtu(23), _rxHead(0), _rxTail(0),
      _server(nullptr), _txChar(nullptr), _rxChar(nullptr) {
    _errorText[0] = '\0';
}

ConfigBLE::~ConfigBLE() { disable(); }

/* ------------------------------------------------------------------------- */
/* Deferred requests                                                          */
/* ------------------------------------------------------------------------- */

void ConfigBLE::requestEnable() { _wantEnable = true; _wantDisable = false; }
void ConfigBLE::requestDisable() { _wantDisable = true; _wantEnable = false; }

void ConfigBLE::requestToggle() {
    if (_enabled || _wantEnable) {
        _wantDisable = true;
        _wantEnable = false;
    } else {
        _wantEnable = true;
        _wantDisable = false;
    }
}

void ConfigBLE::update() {
    if (_wantEnable) {
        _wantEnable = false;
        if (!_enabled) enable();
    }
    if (_wantDisable) {
        _wantDisable = false;
        if (_enabled) disable();
    }

    /* Refresh the bond count at most every 2 s, in this (main) task. */
    if (_enabled) {
        uint32_t now = millis();
        if ((int32_t)(now - _bondCheckAt) >= 0) {
            _bondCheckAt = now + 2000;
            int n = esp_ble_get_bond_device_num();
            _bondCount = (n > 0) ? n : 0;
        }

        /* Advertising supervision. The controller can stop advertising
         * without a disconnect event (failed first start, a stop issued by
         * another module, a link that died silently). Without this the switch
         * still reads "on" while the device is invisible to phones. */
        if (!_connected && !_advertising && !_starting &&
            (int32_t)(now - _advRetryAt) >= 0) {
            _advRetryAt = now + 5000;
            BLE_LOG("advertising not running, restarting (err='%s')\n", _errorText);
            _startAdvertising();
        }
    } else {
        _bondCount = 0;
    }
}

String ConfigBLE::getConnectedAddress() {
    if (!_stateMutex) return String("");
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    String copy = _connectedAddr;
    xSemaphoreGive(_stateMutex);
    return copy;
}

BleUiState ConfigBLE::state() const {
    if (_starting) return BleUiState::Starting;
    if (!_enabled) return BleUiState::Off;
    if (_connected) return BleUiState::Connected;
    if (_errorText[0] != '\0') return BleUiState::Error;
    /* Only report Advertising when the controller is really advertising. */
    return _advertising ? BleUiState::Advertising : BleUiState::Starting;
}

const char* ConfigBLE::stateText() const {
    switch (state()) {
    case BleUiState::Off: return "Off";
    case BleUiState::Starting: return "Starting";
    case BleUiState::Advertising: return "Advertising";
    case BleUiState::Connected: return "Connected";
    case BleUiState::Error: return "Error";
    }
    return "Off";
}


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

    _starting = true;
    _errorText[0] = '\0';
    _advertising = false;

    /* Log the stack state and free heap so a low-memory cause after repeated
     * toggles is visible in the serial log. */
    esp_bluedroid_status_t before = esp_bluedroid_get_status();
    BLE_LOG("enable: bluedroid=%d name='%s' freeHeap=%u\n",
            (int)before, _deviceName.c_str(), (unsigned)esp_get_free_heap_size());

    BLEDevice::init(_deviceName.c_str());

    esp_bluedroid_status_t after = esp_bluedroid_get_status();
    BLE_LOG("enable: bluedroid after init=%d heap=%u psram=%u\n",
            (int)after, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    if (after != ESP_BLUEDROID_STATUS_ENABLED) {
        snprintf(_errorText, sizeof(_errorText), "BLE stack init failed (%d)", (int)after);
        _starting = false;
        _enabled = false;
        BLE_LOG("ERROR: %s\n", _errorText);
        return;
    }

    /* Set the name explicitly as well: BLEDevice::init() does this, but
     * repeating it gives us the real esp_err_t in the log. */
    esp_err_t nameErr = esp_ble_gap_set_device_name(_deviceName.c_str());
    BLE_LOG("enable: set_device_name err=%d\n", (int)nameErr);
    if (nameErr != ESP_OK) {
        snprintf(_errorText, sizeof(_errorText), "set name failed (%d)", (int)nameErr);
        _starting = false;
        _enabled = false;
        BLE_LOG("ERROR: %s\n", _errorText);
        return;
    }

    BLEDevice::setMTU(517);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(new BLE_SecCallbacks(this));

    _server = BLEDevice::createServer();
    if (_server == nullptr) {
        snprintf(_errorText, sizeof(_errorText), "GATT server create failed");
        _starting = false;
        _enabled = false;
        BLE_LOG("ERROR: %s\n", _errorText);
        return;
    }
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
    _starting = false;
    _startAdvertising();
    _advRetryAt = millis() + 5000;

    if (!_advertising) {
        /* Keep the GATT server up but make the failure visible in Settings. */
        if (_errorText[0] == '\0')
            snprintf(_errorText, sizeof(_errorText), "advertising did not start");
        BLE_LOG("ERROR: %s\n", _errorText);
    } else {
        BLE_LOG("BLE enabled, advertising as '%s'\n", _deviceName.c_str());
    }
}

void ConfigBLE::disable() {
    if (!_enabled) return;

    /* Hide the stack from UI queries before tearing it down: the LVGL task
     * may call getBondCount()/state() concurrently. */
    _enabled = false;
    _starting = true;

    _stopAdvertising();
    _advertising = false;
    _advRetryAt = 0;
    if (_server) {
        /* deinit(true) releases the BT controller memory but leaves
         * BLEDevice's static `initialized` flag set, so the next enable()
         * silently skipped the stack init and advertising never started.
         * deinit(false) resets that flag and allows a clean re-enable. */
        BLEDevice::deinit(false);
    }

    _starting = false;
    _connected = false;
    _secure = false;
    _passkey = 0;
    _server = nullptr;
    _txChar = nullptr;
    _rxChar = nullptr;
    _rxHead = 0;
    _rxTail = 0;
    _errorText[0] = '\0';
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
    if (!_enabled) return;
    int n = esp_ble_get_bond_device_num();
    if (n <= 0) return;
    esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(n * sizeof(esp_ble_bond_dev_t));
    if (!list) return;
    int count = n;
    if (esp_ble_get_bond_device_list(&count, list) != ESP_OK) {
        free(list);
        return;
    }
    for (int i = 0; i < count; i++) esp_ble_remove_bond_device(list[i].bd_addr);
    free(list);
    _bondCount = 0;
    _bondCheckAt = 0;
    BLE_LOG("Cleared %d bond(s)\n", count);
}

int ConfigBLE::getBondCount() {
    /* Cached by update(); safe to call from the LVGL task. */
    return _enabled ? _bondCount : 0;
}

bool ConfigBLE::getBondAddress(int index, String& out) {
    if (!_enabled || index < 0) return false;
    int n = _bondCount;
    if (index >= n || n <= 0) return false;

    esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(n * sizeof(esp_ble_bond_dev_t));
    if (!list) return false;
    int count = n;
    if (esp_ble_get_bond_device_list(&count, list) != ESP_OK) {
        free(list);
        return false;
    }

    bool ok = false;
    if (index < count) {
        char buf[4];
        out = "";
        for (int i = 0; i < 6; i++) {
            snprintf(buf, sizeof(buf), "%02X", list[index].bd_addr[i]);
            out += buf;
            if (i < 5) out += ":";
        }
        ok = true;
    }
    free(list);
    return ok;
}

bool ConfigBLE::removeBond(int index) {
    if (!_enabled || index < 0) return false;
    int n = esp_ble_get_bond_device_num();
    if (index >= n || n <= 0) return false;

    esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(n * sizeof(esp_ble_bond_dev_t));
    if (!list) return false;
    int count = n;
    if (esp_ble_get_bond_device_list(&count, list) != ESP_OK) {
        free(list);
        return false;
    }

    bool ok = false;
    if (index < count) {
        ok = (esp_ble_remove_bond_device(list[index].bd_addr) == ESP_OK);
        BLE_LOG("Bond %d %s removed\n", index, ok ? "" : "NOT");
    }
    free(list);
    _bondCheckAt = 0;   /* refresh the cache on the next update() */
    return ok;
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
    if (!_enabled || _advertising)
        return;

    /* Raw GAP data is used instead of BLEAdvertising so every step returns a
     * real esp_err_t. Advertisement: flags + complete 128-bit NUS service.
     * Scan response: complete local name, which is what phones display. */
    uint8_t adv[31];
    size_t ai = 0;
    adv[ai++] = 0x02;
    adv[ai++] = 0x01;   /* flags */
    adv[ai++] = (uint8_t)(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);

    BLEUUID uuid(NUS_SERVICE_UUID);
    const uint8_t *u = uuid.getNative()->uuid.uuid128;
    adv[ai++] = 0x11;   /* length: type + 16 bytes */
    adv[ai++] = 0x07;   /* complete list of 128-bit service UUIDs */
    memcpy(&adv[ai], u, 16);
    ai += 16;

    /* Also carry a short name in the advertisement itself (not only in the
     * scan response). Some phones show passive-scan results without sending
     * a scan request; without this they list an unnamed device. 30 bytes total
     * keeps the packet inside the 31-byte legacy limit. */
    static const char SHORT_NAME[] = "Chappie";
    adv[ai++] = (uint8_t)(sizeof(SHORT_NAME));   /* length byte: type + 7 chars */
    adv[ai++] = 0x08;                            /* shortened local name */
    memcpy(&adv[ai], SHORT_NAME, sizeof(SHORT_NAME) - 1);
    ai += sizeof(SHORT_NAME) - 1;

    uint8_t rsp[31];
    size_t ri = 0;
    size_t nameLen = _deviceName.length();
    if (nameLen > 28)
        nameLen = 28;
    rsp[ri++] = (uint8_t)(nameLen + 1);
    rsp[ri++] = 0x09;   /* complete local name / device name */
    memcpy(&rsp[ri], _deviceName.c_str(), nameLen);
    ri += nameLen;

    esp_err_t e1 = esp_ble_gap_config_adv_data_raw(adv, (uint32_t)ai);
    esp_err_t e2 = esp_ble_gap_config_scan_rsp_data_raw(rsp, (uint32_t)ri);

    esp_ble_adv_params_t params = {};
    params.adv_int_min = 0x20;      /* 20 ms */
    params.adv_int_max = 0x40;      /* 40 ms */
    params.adv_type = ADV_TYPE_IND; /* connectable undirected */
    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.channel_map = ADV_CHNL_ALL;
    params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    esp_err_t e3 = esp_ble_gap_start_advertising(&params);

    BLE_LOG("advertising: cfg=%d scanRsp=%d start=%d name='%s' advLen=%u rspLen=%u\n",
            (int)e1, (int)e2, (int)e3, _deviceName.c_str(),
            (unsigned)ai, (unsigned)ri);

    if (e1 == ESP_OK && e2 == ESP_OK && e3 == ESP_OK) {
        _advertising = true;
        _errorText[0] = '\0';
    } else {
        _advertising = false;
        snprintf(_errorText, sizeof(_errorText), "adv start err %d/%d/%d",
                 (int)e1, (int)e2, (int)e3);
    }
}

void ConfigBLE::_stopAdvertising() {
    if (!_advertising)
        return;
    esp_err_t err = esp_ble_gap_stop_advertising();
    BLE_LOG("advertising stopped: err=%d\n", (int)err);
    _advertising = false;
}
