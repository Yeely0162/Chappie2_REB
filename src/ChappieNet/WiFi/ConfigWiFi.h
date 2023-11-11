#ifndef CONFIGWIFI_H
#define CONFIGWIFI_H
#include <WiFi.h> // 引入 WiFi 库
// #include "WiFiClient.h"
#include "ChappieBsp/Utility/BM8563/I2C_BM8563.h"
#include <DNSServer.h>
#include <WebServer.h>
#include <HttpClient.h>
#include <ESPmDNS.h>  //用于设备域名 MDNS.begin("esp32")
// #include <esp_wifi.h> //用于esp_wifi_restore() 删除保存的wifi信息
#include <Preferences.h> //用于参数数据的
#define ROOT_HTML  "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"/><title>WiFi配置</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css\"><style>body {font-family: 'Microsoft YaHei', sans-serif;padding-top: 5%;}.rounded-form {border-radius: 15px;border: 1px solid #d1d1d1;padding: 20px;background-color: #f4f4f4;}.rounded-input {border-radius: 15px;}.rounded-btn {border-radius: 15px;background-color: #007BFF;color: white;border: none;}#wifiList {list-style-type: none; padding: 0;}#wifiList li {padding: 10px;border: 1px solid #d1d1d1;border-radius: 10px;margin-bottom: 5px;cursor: pointer;transition: background-color 0.3s;}#wifiList li:hover {background-color: #e1e1e1; }</style></head><body><div class=\"container\"><div class=\"row justify-content-center\"><div class=\"col-md-6\"><h2 class=\"text-center\">WiFi配置</h2><div class=\"rounded-form\"><form method=\"POST\" action=\"configwifi\"><div class=\"form-group\"><label for=\"ssid\">WiFi SSID:</label><input type=\"text\" class=\"form-control rounded-input\" id=\"ssid\" name=\"ssid\"></div><div class=\"form-group\"><label for=\"pass\">WiFi 密码:</label><input type=\"password\" class=\"form-control rounded-input\" id=\"pass\" name=\"pass\"></div><button type=\"submit\" class=\"btn rounded-btn btn-block\">提交</button></form><h4 class=\"mt-4\">附近的WiFi:</h4><ul id=\"wifiList\">"

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
String GetIPv6();
String requesturl(const char * Domain , uint16_t DSport,const char * url);

private:
const char *HOST_NAME="Chappie-Kalicyh";
const char* ntpServer = "ntp.aliyun.com";
};
#define WiFi_LOG(format, args...) printf(format, ##args)
#endif
