/*
 Huge thank you to Uspizig for offering this library that I have modified for TFT_eSPI: https://github.com/Uspizig/MLX90640
 Has to be Library, TFT_eSPI Rev 2.5.43
 The latest does not work

*/

#include <TFT_eSPI.h> 
#include <SPI.h>
#include <Wire.h>
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"
#include "CST816T.h"

#define TA_SHIFT 8 //Default shift for MLX90640 in open air
#define MLX_VDD  11
#define MLX_SDA  41
#define MLX_SCL  42
#define TOUCH_SDA 7
#define TOUCH_SCL 6
#define TOUCH_RST 5


#define SCREEN_ROTATION 1
#define BAT_ADC  10
#define SCREEN_BL_PIN 9

#define _SCALE 9
#define BTN_LONG_PUSH_T 1000
#define TOUCH_LONG_PUSH_T 200
#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  5        /* Time ESP32 will go to sleep (in seconds) */
#define GRIDCOLOR  0xA815


const int buttonPin1 = 0;  
const int buttonPin2 = 21; 


bool buttonState1 = 1;  
bool buttonState2 = 1;  

const byte MLX90640_address = 0x33;
static float mlx90640To[768];  // 从MLX90640读取的温度数据
static float mlx90640To_buffer[768];  // 缓存区域，复制MLX90640读取的温度数据并用于绘制热力图
paramsMLX90640 mlx90640;

static uint16_t heat_bitmap[32*_SCALE * 24*_SCALE] = {}; // rgb56556形式的内存，用于存储要渲染的图像

uint16_t test_points[5][2];
int brightness = 128;

int R_colour, G_colour, B_colour;            
// int i, j;                                    
float T_max, T_min, T_avg;                            
float T_center;  
int max_x, max_y, min_x, min_y;

float bat_v;

bool lock = false;  // 简单的锁，防止拷贝温度数据的时候对内存的访问冲突
bool touch_updated = false;

bool power_on = true;  // 是否开机
bool freeze = false;  // 暂停画面
bool show_local_temp_flag = true;  // 是否显示点测温
bool clear_local_temp = false;     // 点测温清除

TFT_eSPI tft = TFT_eSPI();  
CST816T touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, -1);	// sda, scl, rst, irq

int diffx, diffy;

// ===============================
// ===== determine the colour ====
// ===============================

// const char* str_wake[50];
// // 打印唤醒原因
// void print_wakeup_reason(){
//   esp_sleep_wakeup_cause_t wakeup_reason;

//   wakeup_reason = esp_sleep_get_wakeup_cause();

//   switch(wakeup_reason)
//   {
//     case ESP_SLEEP_WAKEUP_EXT0 : strcpy("Wakeup caused by external signal using RTC_IO", str_wake); break;
//     case ESP_SLEEP_WAKEUP_EXT1 : strcpy("Wakeup caused by external signal using RTC_CNTL", str_wake); break;
//     case ESP_SLEEP_WAKEUP_TIMER : strcpy("Wakeup caused by timer", str_wake); break;
//     case ESP_SLEEP_WAKEUP_TOUCHPAD : strcpy("Wakeup caused by touchpad", str_wake); break;
//     case ESP_SLEEP_WAKEUP_ULP : strcpy("Wakeup caused by ULP program", str_wake); break;

//     default : sprintf(str_wake, "Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
//   }
// }


void getColour(int j)
   {
    if (j >= 0 && j < 30)
       {
        R_colour = 0;
        G_colour = 0;
        B_colour = 20 + (120.0/30.0) * j;
       }
    
    if (j >= 30 && j < 60)
       {
        R_colour = (120.0 / 30) * (j - 30.0);
        G_colour = 0;
        B_colour = 140 - (60.0/30.0) * (j - 30.0);
       }

    if (j >= 60 && j < 90)
       {
        R_colour = 120 + (135.0/30.0) * (j - 60.0);
        G_colour = 0;
        B_colour = 80 - (70.0/30.0) * (j - 60.0);
       }

    if (j >= 90 && j < 120)
       {
        R_colour = 255;
        G_colour = 0 + (60.0/30.0) * (j - 90.0);
        B_colour = 10 - (10.0/30.0) * (j - 90.0);
       }

    if (j >= 120 && j < 150)
       {
        R_colour = 255;
        G_colour = 60 + (175.0/30.0) * (j - 120.0);
        B_colour = 0;
       }

    if (j >= 150 && j <= 180)
       {
        R_colour = 255;
        G_colour = 235 + (20.0/30.0) * (j - 150.0);
        B_colour = 0 + 255.0/30.0 * (j - 150.0);
       }
}

void print_bitmap(int x, int y, uint16_t * data){
   
   for(int i=0; i<y; i++){
      for(int j=0; j<x; j++){
         Serial.print(data[i*j]);
         Serial.print(" ");
      }
      Serial.print("\n");
   }
   Serial.print("\n\n");
}



//Returns true if the MLX90640 is detected on the I2C bus
boolean isConnected()
   {
    Wire.beginTransmission((uint8_t)MLX90640_address);
    if (Wire.endTransmission() != 0){return (false);}
    return (true);
   }   

// 绘制十字
void draw_cross(int x, int y, int len){
   tft.drawLine(x - len/2, y, x + len/2, y, tft.color565(255, 255, 255));
   tft.drawLine(x, y-len/2, x, y+len/2,  tft.color565(255, 255, 255));

   tft.drawLine(x - len/4, y, x + len/4, y, tft.color565(0, 0, 0));
   tft.drawLine(x, y-len/4, x, y+len/4,  tft.color565(0, 0, 0));
}

// 点测温功能
void show_local_temp(int x, int y){
   draw_cross(x, y, 10);
   float temp_xy = mlx90640To[(24 - y / _SCALE) * 32 + (x / _SCALE)];
   int shift_x, shift_y;
   if (x<140){shift_x=10;} else {shift_x=-40;}
   if (y<120){shift_y=10;} else {shift_y=-10;}
   tft.setCursor(x+shift_x, y+shift_y);
   tft.printf("%.2f", temp_xy);
}  

// void draw_heat_image(bool re_mapcolor=true){
//    tft.setRotation(3);
//    for (int i = 0 ; i < 24 ; i++){
//    for (int j = 0; j < 32; j++){
//       if (re_mapcolor) {mlx90640To_buffer[i*32 + j] = 180.0 * (mlx90640To_buffer[i*32 + j] - T_min) / (T_max - T_min);}
//       getColour(mlx90640To_buffer[i*32 + j]);
//       tft.fillRect(280 - j * _SCALE, (240 - _SCALE * 24) + i * _SCALE, _SCALE, _SCALE, tft.color565(R_colour, G_colour, B_colour));  
//       // tft.drawBitmap();
//       }
//    }
//    tft.setRotation(SCREEN_ROTATION);
// }

// 在一块图像内存上绘制一个同色的方块
void draw_block_bitmap(int x, int y, int w, int max_x, int max_y, uint16_t color, uint16_t* data){
   for(int i =y; i < y + w; i++){
        for(int j = x; j < x + w; j++){
         data[i * max_x + j] = color;
      }
   }
}

// 更新图像内存中的图像
void update_bitmap(bool re_mapcolor=true){
   for(int y=0; y<24; y++){ 
      for(int x=0; x<32; x++){
         int id = (23-y) * 32 + x;
         if (re_mapcolor) {mlx90640To_buffer[id] = 180.0 * (mlx90640To_buffer[id] - T_min) / (T_max - T_min);}
         getColour(mlx90640To_buffer[id]);
         draw_block_bitmap(x*_SCALE, y*_SCALE, _SCALE, 32*_SCALE, 24*_SCALE, tft.color565(R_colour, G_colour, B_colour), heat_bitmap);
      } 
   }
}

// 在屏幕上绘制热力图
void draw_heat_image(bool re_mapcolor=true){
   // tft.setRotation(3);
   tft.setRotation(SCREEN_ROTATION);
   update_bitmap(re_mapcolor);
   tft.pushImage(0, 0, 32*_SCALE, 24*_SCALE, heat_bitmap);
}


// 热成像读取多任务
void task_mlx(void * ptr){
   Wire.begin(MLX_SDA, MLX_SCL); 
   pinMode(MLX_VDD, OUTPUT);
   digitalWrite(MLX_VDD, LOW);
   vTaskDelay(1000);
   Wire.setClock(800000); //Increase I2C clock speed to 400kHz

   Serial.println("MLX90640 IR Array Example");

   if (isConnected() == false){
      while (1){
         Serial.println("MLX90640 not detected at default I2C address. Please check wiring. Freezing.");
         vTaskDelay(1000);
         };
   }Serial.println("MLX90640 online!");
   int status;
   uint16_t eeMLX90640[832];
      
   status = MLX90640_DumpEE(MLX90640_address, eeMLX90640);
   if (status != 0)
         Serial.println("Failed to load system parameters");

   status = MLX90640_ExtractParameters(eeMLX90640, &mlx90640);
   if (status != 0)
   {
      Serial.println("Parameter extraction failed");
      Serial.print(" status = ");
      Serial.println(status);
   }
   //Once params are extracted, we can release eeMLX90640 array
      MLX90640_I2CWrite(0x33, 0x800D, 6401);    // writes the value 1901 (HEX) = 6401 (DEC) in the register at position 0x800D to enable reading out the temperatures!!!
   // ===============================================================================================================================================================
   //MLX90640_SetRefreshRate(MLX90640_address, 0x00); //Set rate to 0.25Hz effective - Works
   //MLX90640_SetRefreshRate(MLX90640_address, 0x01); //Set rate to 0.5Hz effective - Works
   //MLX90640_SetRefreshRate(MLX90640_address, 0x02); //Set rate to 1Hz effective - Works
   //MLX90640_SetRefreshRate(MLX90640_address, 0x03); //Set rate to 2Hz effective - Works
   //MLX90640_SetRefreshRate(MLX90640_address, 0x04); //Set rate to 4Hz effective - Works
   MLX90640_SetRefreshRate(MLX90640_address, 0x05); //Set rate to 8Hz effective - Works at 800kHz
   //MLX90640_SetRefreshRate(MLX90640_address, 0x06); //Set rate to 16Hz effective - fails
   //MLX90640_SetRefreshRate(MLX90640_address, 0x07); //Set rate to 32Hz effective - fails

   // MLX主循环
   for(;power_on==true;){
      if (!freeze){ // 如果画面被暂停会跳过这个热成像图的刷新
         lock = true;
         for (byte x = 0 ; x < 2 ; x++){
            uint16_t mlx90640Frame[834];
            int status = MLX90640_GetFrameData(MLX90640_address, mlx90640Frame);
         
            if (status < 0){
               Serial.print("GetFrame Error: ");
               Serial.println(status);
               }
            float vdd = MLX90640_GetVdd(mlx90640Frame, &mlx90640);
            float Ta = MLX90640_GetTa(mlx90640Frame, &mlx90640);

            float tr = Ta - TA_SHIFT; //Reflected temperature based on the sensor ambient temperature
            float emissivity = 0.95;

            MLX90640_CalculateTo(mlx90640Frame, &mlx90640, emissivity, tr, mlx90640To);
         }
            // determine T_min and T_max and eliminate error pixels
            // ====================================================

         // mlx90640To[1*32 + 21] = 0.5 * (mlx90640To[1*32 + 20] + mlx90640To[1*32 + 22]);    // eliminate the error-pixels
         // mlx90640To[4*32 + 30] = 0.5 * (mlx90640To[4*32 + 29] + mlx90640To[4*32 + 31]);    // eliminate the error-pixels
         
         lock = false;

         T_min = mlx90640To[0];
         T_max = mlx90640To[0];
         T_avg = mlx90640To[0];
         for (int i = 1; i < 768; i++){
            if((mlx90640To[i] > -41) && (mlx90640To[i] < 301))
               {
                  if(mlx90640To[i] < T_min)
                     {
                     T_min = mlx90640To[i];
                     }

                  if(mlx90640To[i] > T_max)
                     {
                     T_max = mlx90640To[i];
                     max_x = i / 32;
                     max_y = i % 32;
                     }
               }
            else if(i > 0)   // temperature out of range
               {
                  mlx90640To[i] = mlx90640To[i-1];
               }
            else
               {
                  mlx90640To[i] = mlx90640To[i+1];
               }
               T_avg = T_avg + mlx90640To[i];
            }
         T_avg = T_avg / 768;
         }
      vTaskDelay(5);
   }
   vTaskDelete(NULL); 
}

// 关机
void power_off(){
   // pinMode(GPIO_NUM_33, INPUT_PULLUP);
   // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
   // esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0); //1 = High, 0 = Low
   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
   // rtc_gpio_isolate(MLX_VDD);
   // rtc_gpio_isolate(SCREEN_BL_PIN);
   // rtc_gpio_isolate(MLX_SDA);
   // rtc_gpio_isolate(MLX_SCL);
   // rtc_gpio_isolate(TOUCH_SDA);
   // rtc_gpio_isolate(TOUCH_SCL);
   // rtc_gpio_isolate(TOUCH_RST);
   
   // esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
   // esp_deep_sleep_start();

   for(int i=brightness; i>0; i--){
      ledcWrite(0, i);
      vTaskDelay(2);
   }
   
   power_on = false;
   digitalWrite(MLX_VDD, LOW);
   ledcWrite(0, 0);
   digitalWrite(SCREEN_BL_PIN, LOW);
   vTaskDelay(2000);
   // esp_light_sleep_start();
   esp_deep_sleep_start();
   vTaskDelete(NULL); 
}

// 背光调节,会限制输入亮度在正确范围内
void set_brightness(int _brightness){
   if (_brightness < 1024 && _brightness > 5){
      ledcWrite(0, _brightness);
      brightness = _brightness;
   }else if(_brightness >= 1024){ledcWrite(0, 1023); brightness=1023;
   }else if(_brightness <= 5)   {ledcWrite(0, 5); brightness=5;
   }
}


// 平滑的开机
void task_smooth_on(void * ptr){
   ledcSetup(0, 3000, 10);
   ledcAttachPin(SCREEN_BL_PIN, 0);
   ledcWrite(0, 0);
   vTaskDelay(100);
   for(int i=0; i<brightness; i++){
      ledcWrite(0, i);
      vTaskDelay(2);
   }
   vTaskDelete(NULL); 
}


// 电池管理多任务
void task_bat(void * ptr){
   pinMode(BAT_ADC, INPUT);
   float r1 = 300.;
   float r2 = 680.;
   float coef = (r1+r2) / r2;
   for(;power_on==true;){
      int adc_value = analogRead(BAT_ADC);
      bat_v = (float)adc_value / 4096 * 3.3 * coef;
      vTaskDelay(1000);
   }
   vTaskDelete(NULL);
}

void task_button(void * ptr){
   pinMode(buttonPin1, INPUT_PULLUP);
   pinMode(buttonPin2, INPUT_PULLUP);
   unsigned long btn1_pushed_start_time =  0;
   bool btn2_pushed = false;
   bool btn1_pushed = false;
   for(;power_on==true;){
      
      if (digitalRead(buttonPin1) == LOW){  // 长按btn1的关机功能
         if (millis() - btn1_pushed_start_time >= BTN_LONG_PUSH_T){
            power_off();
         } 
         vTaskDelay(5);
         if (digitalRead(buttonPin1) == LOW){btn1_pushed=true;}
      }else{
         btn1_pushed_start_time = millis();
         if (btn1_pushed) {  // 短按btn1
            test_points[0][0] = 0;
            test_points[0][1] = 0;
            if (freeze==true){ clear_local_temp=true; }
            }
         btn1_pushed=false;
      }

      if (digitalRead(buttonPin2) == LOW){
         vTaskDelay(5);
         if (digitalRead(buttonPin2) == LOW){btn2_pushed=true;}
      }else{
         if (btn2_pushed) {freeze = !freeze; }
         btn2_pushed=false;
      }

      buttonState1 = digitalRead(buttonPin1);
      buttonState2 = digitalRead(buttonPin2);
      vTaskDelay(100); 
   }
   vTaskDelete(NULL);
}

void task_touchpad(void * ptr){
   // touch.begin();
   uint16_t x, y;
   uint16_t start_x, start_y;
   bool long_pushed = false;
   unsigned long touch_pushed_start_time =  0;
   bool touched = false;
   int start_br = brightness;
   for(;power_on==true;){
      if (touch_updated) {
         if( touch.tp.touching )
         {
            x= touch.tp.y;
            y = 240 - touch.tp.x;
            if (touched==false){start_x = x;  start_y = y; diffy=0; diffx=0;}  // 下降沿
            if (millis() - touch_pushed_start_time >= TOUCH_LONG_PUSH_T){
               long_pushed = true;
               diffx= start_x-x;
               diffy= start_y-y;
               set_brightness(start_br+diffy*5);
               }else{ // 短按的中间
                  
               }
         }else{
            touch_pushed_start_time = millis();
            if (touched==true){  // 上升沿
               if (start_br == brightness){
                  if (y < 216){test_points[0][0] = x; test_points[0][1] = y;}
               }
               if (long_pushed==false){  // 短按时
                  if (y < 216){test_points[0][0] = x; test_points[0][1] = y;}
               }
               start_br = brightness;
               long_pushed = false;  // 上升沿将长按检测标识符进行复位
            }  
         }
         // Serial.printf("touch: %d %d\n", touched, touch.tp.touching);
         touched = touch.tp.touching;
         touch_updated = false;
      }
      vTaskDelay(10);
   }
   vTaskDelete(NULL);
}


void task_screen_draw(void * ptr){
   tft.setRotation(SCREEN_ROTATION);
   tft.fillScreen(TFT_BLACK);
   test_points[0][0] = 120;
   test_points[0][1] = 110;

   // tft.setBitmapColor(16);
   for(;power_on==true;){
      // 这么做是预防touch和spi总线发生冲突
      if (!touch_updated){touch.update(); touch_updated=true;}

      // read the state of the pushbutton value:
      buttonState1 = digitalRead(buttonPin1);
      buttonState2 = digitalRead(buttonPin2);

      if (!freeze){ // 如果画面被暂停会跳过这个热成像图的刷新
         // 只有画面更新才会绘制一张热成像图
         while(lock && power_on){
            // 阻塞画面
            vTaskDelay(1);
         }
         for (int i = 0; i < 768; i++) {mlx90640To_buffer[i] = mlx90640To[i];}  // 拷贝温度信息
         draw_heat_image();
      }

      tft.setRotation(SCREEN_ROTATION);
      if (test_points[0][0]==0 && test_points[0][1]==0 ){}else{show_local_temp(test_points[0][0], test_points[0][1]);}
      // if (show_local_temp_flag==true) {show_local_temp(test_points[0][0], test_points[0][1]);}
      if (clear_local_temp==true) {draw_heat_image(false); clear_local_temp=false;}
      // tft.endWrite();

      tft.setRotation(SCREEN_ROTATION);
      tft.setTextColor(TFT_WHITE, TFT_BLACK); 
      tft.setCursor(40, 220);
      tft.printf("max: %.2f  ", T_max);
      tft.setCursor(40, 230);
      tft.printf("min: %.2f  ", T_min);
      
      tft.setCursor(120, 220);
      tft.printf("avg: %.2f  ", T_avg);
      tft.setCursor(120, 230);
      tft.printf("bat: %.2f v ", bat_v);
      
      tft.setCursor(190, 220);
      tft.printf("  brightness");
      tft.setCursor(190, 230);
      tft.printf("       %d  ", brightness);

      tft.setCursor(155, 220);
      //  tft.print("C");
      
      //  tft.setCursor(80, 200);
      //  tft.printf("max_pos: %d, %d ", max_x, max_y);
      //  tft.setCursor(80, 210);
      //  tft.printf("touch_state: %d, %d     ",test_points[0][0], test_points[0][1]);
      // tft.setCursor(5, 210);
      // // tft.printf("button: %d, %d     ",buttonState1, buttonState2);
      // tft.printf(str_wake);

      vTaskDelay(10);
   }
   vTaskDelete(NULL);
}

void setup(void)
 {
   // Serial.begin(115200);
   touch.begin();
   // 按钮启用

   pinMode(SCREEN_BL_PIN, OUTPUT);
   
   xTaskCreate(task_mlx, "MLX_FLASHING", 1024 * 8, NULL, 1, NULL);
   xTaskCreate(task_bat, "BAT_MANAGER", 1024 * 2, NULL, 3, NULL);
   tft.init();
   tft.setSwapBytes(true);
   xTaskCreate(task_screen_draw, "SCREEN", 1024 * 10, NULL, 2, NULL);
   xTaskCreate(task_smooth_on, "SMOOTH_ON", 1024, NULL, 2, NULL);
   xTaskCreate(task_button,    "BUTTON", 1024 * 6, NULL, 3, NULL);
   xTaskCreate(task_touchpad,  "TOUCHPAD", 1024 * 6, NULL, 3, NULL);
   // draw the colour-scale
}

void loop() 
{
 vTaskDelay(3000);
}
