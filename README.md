# Chappie2 Rebuild Project for AXP173

###### 注意⚠️工程为PIO工程，vscode安装PIO扩展后打开文件夹即可



#### 此代码可以提供给原版Chappie2 以及我修改的AXP173版Chappie2

#####  ⚠️注意，如果原版Chappie2没用BMP280，请在初始化中注释他`Chappie.cpp` 否则无法进入桌面

#####  主要修改的文件是 `ChappiePower.hpp`

```c++
//原版Chiappie2修改下面定义
#define AXPManage 0             
#define IP5Manage 1
/* Init BMP280 */
//Env.init();
```



### 库的引用

|     库名     |               功能               |   作者\|公司    |
| :----------: | :------------------------------: | :-------------: |
| ArduinoJson  |           提取json数据           |     Arduino     |
| ResourcePool | 用于图片、字体的声明、调用功能器 | X-Track\|源代码 |
|    AXP173    |            控制AXP173            |   Forairaaaa    |



### 项目修改

|      项目 APP      |           功能            |                  修改\|创建                  |             Bug\|缺陷             | 完善 |
| :----------------: | :-----------------------: | :------------------------------------------: | :-------------------------------: | :--: |
|    Noiser(噪声)    |     用于统计环境噪声      |                     创建                     |  无法准确统计实际噪声，误差很大   |  ✖️   |
| Synology(群晖仪表) |     用于监控群晖状态      |                修改了仪表样式                |           不完善，能用            |  ✖️   |
|   Setting(设置)    |  用于设备的基本控制参数   |       创建了主页面、添加了WiFi基本功能       |          其他功能未设置           |  ✖️   |
|  LEDS(艾漏ED灯嘶)  |       控制LED的颜色       |                  创建了色环                  |      颜色有反转和LED型号有关      |  ✔️   |
|     App_Lunch      | App主界面（非必要别修改） | 时间，日期，时间更新，wifi按钮，自动休眠时间 |       更新时间会卡住页面2S        |  ✔️   |
|    StupidSnake     |        贪吃蛇游戏         |                    修复了无法移动问题                    | 无 |  ✔️   |
|       Watch        |           表盘            |       创建了新表盘（仿iwatch充电表盘）       |  请不用连续快速进退Watch，会BooM  |  --  |



#### 群晖配置注意

> 1. 首先进入文件 `App_Synology.cpp` 将❌❌❌ 地方改为自己相对应的地方即可
> 2. 进入群晖->账户设置->新账户 ->给此账户禁止访问`群晖`和某些`隐私文件夹`->对用户进行委派权限即可(除了用户组管理的，都可以委派,~~委派的用户UPS访问无权限~~ ) (无所谓的大佬，直接用admin管理组)
> 3. 烧录完成之后，首先打开WiFi，进入程序默默等待即可，有初始化步骤，莫着急！





####  欢迎各位大佬对项目多种二创和创新，可以fork我的库，在二创的同时也可以commit给我合并代码，让大家都能互相分享自己的Idea，同时也可以支持一下我，给我点个Star🌟，同时也要感谢源UP开源的程序和PCB

####  源UP项目地址：[Chappie2](https://github.com/Forairaaaaa/Chappie-II)



##### ⚠️为了部分复刻友友因没有梯子，所以Lib\Src文件夹里没用删除依赖和引用文件，所以Clone下来可以直接编译，所以下载的整体文件会比较大，请谅解！

#### 下载依赖

```shell
git submodule init
git submodule update
```

```shell
cd lib
git clone https://github.com/lovyan03/LovyanGFX.git
git clone -b v8.3.5 https://github.com/lvgl/lvgl.git
git clone https://github.com/FastLED/FastLED.git
git clone https://github.com/lemmingDev/ESP32-BLE-Gamepad.git
git clone https://github.com/h2zero/NimBLE-Arduino.git
git clone https://github.com/kosme/arduinoFFT.git
```



#### 一些问题？

> 1.项目代码整体来看还是可以的不算很乱，没有整合所有图标和字体到ResourcePool
>
> 2.主界面有些卡顿？ 目前来说是因为，使用的图标图片选用、格式导致的，所以整体流畅度削弱，看起来稍微掉帧，但不影响使用，可以修改一些简单的图标样式来缓解这个问题！
>
> 3.待补充...（欢迎大家提issue,也希望大家帮忙解决）
