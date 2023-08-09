#ifndef CONFIGWIFI_H
#define CONFIGWIFI_H
#include <WiFi.h> // 引入 WiFi 库
// #include "WiFiClient.h"
#include "ChappieBsp\Utility\BM8563\I2C_BM8563.h"
#include <DNSServer.h>
#include <WebServer.h>
#include"HttpClient.h"
#include <ESPmDNS.h>      //用于设备域名 MDNS.begin("esp32")
// #include <esp_wifi.h>     //用于esp_wifi_restore() 删除保存的wifi信息
#include <Preferences.h> //用于参数数据的
#define ROOT_HTML  "<!DOCTYPE html><html><head><title>WIFI</title><meta name=\"viewport\"content=\"width=device-width, initial-scale=1\"></head><style type=\"text/css\">.input{display:block;margin-top:10px}.input span{width:100px;float:left;float:left;height:36px;line-height:36px}.input input{height:30px;width:200px}.btn{width:120px;height:35px;background-color:#000000;border:0px;color:#ffffff;margin-top:15px;margin-left:100px}</style><body><form method=\"POST\"action=\"configwifi\"><label class=\"input\"><span>WiFi SSID</span><input type=\"text\"name=\"ssid\"value=\"\"></label><label class=\"input\"><span>WiFi PASS</span><input type=\"text\"name=\"pass\"></label><input class=\"btn\"type=\"submit\"name=\"submit\"value=\"Submie\"></form></body></html>\""
static String WiFi_Name;
static String WiFi_Pass;

class ConfigWiFi {
public:
    bool AutoTime = true;
    bool wifiEnabled = false;
    const char* AP_SSID  = "Chappie-AP";
    ConfigWiFi();
    ~ConfigWiFi();
    void Init();
    void enableWiFi();
    void disableWiFi();
    bool isWiFiEnabled() const;
    bool isConnected() const;
    bool APMode();
    bool APClose();
    void NtpTimeCorr();
    void WiFiloop();
    const char * WiFiN();
    String LocalIP();
    String GateWay();
    const char * WiFiMac();
    String requesturl(const char * Domain , uint16_t DSport,const char * url);
    
private:
    const char *HOST_NAME="Chappie-Yeely";
    const char* ntpServer = "ntp.aliyun.com";
};
#define WiFi_LOG(format, args...) printf(format, ##args)
#endif
