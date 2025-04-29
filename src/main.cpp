#include <Arduino.h>
#include <U8g2lib.h>  // OLED显示屏库
#include <DHT.h>      // DHT11温湿度传感器库
#include <DHT_U.h>    // DHT11扩展库
#include <Wire.h>     // I2C通信库
#include <BH1750.h>   // BH1750光照传感器库
#include <OneButton.h> // 按键处理库
#include "SoftwareSerial.h"       //注意添加这个软串口头文件s
#include<WiFi.h>
#include<PubSubClient.h>
//----------------------------------------
// 引脚定义
//----------------------------------------
// 传感器引脚
#define FLAME_SENSOR_PIN 2  // 火焰传感器引脚
#define MQ2_SENSOR_PIN 8    // MQ-2气体传感器引脚
#define DHTPIN 1            // DHT11温湿度传感器引脚
#define VOICE 3            // max4466语音传感器引脚
#define DHTTYPE DHT11       // 使用DHT11型号
#define ZW_IRQ 18           //指纹模块检测引脚
#define ZW_CTRL 11           // 指纹模块启动引脚

// 输出设备引脚
#define LIGHT_PIN 16        // LED灯引脚
#define FAN_PIN 14          // 风扇控制引脚
#define PUMP_PIN 17         // 水泵控制引脚

// 输入设备引脚
#define KEY1 47             // 按键引脚
#define KEY2 38             // 按键引脚
#define KEY3 39             // 按键3引脚

//----------------------------------------
// 全局对象初始化
//----------------------------------------
// 初始化OLED显示屏 SCL-21   SDA-40
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/21, /* data=*/40);

// 初始化DHT11温湿度传感器
DHT dht(DHTPIN, DHTTYPE);

// 初始化BH1750光照传感器
BH1750 lightMeter(0x23);

// 初始化软件串口用于指纹模块通信
SoftwareSerial mySerial(12,13);    //软串口引脚，RX：GPIO12    TX：GPIO13

//----------------------------------------
// 全局变量
//----------------------------------------
bool lightState = false;     // LED灯的当前状态
bool lastButtonState = HIGH; // 按键的上一次状态
bool lastButton3State = HIGH; // 按键3的上一次状态
unsigned long button3PressTime = 0; // 按键3按下的时间
bool button3LongPress = false; // 按键3长按标志

int currentPage = 0;         // 当前页面编号，0为主页面，1为添加指纹页面，2为删除指纹页面
int fingerOption = 0;        // 指纹选项，0为添加指纹，1为删除指纹
int fingerId = 1;            // 当前选择的指纹ID，默认为1
int MAX_FINGER_ID = 6;       // 最大指纹ID数量

// 添加时间管理变量
unsigned long lastSensorReadTime = 0;  // 上次传感器读取时间
const unsigned long sensorReadInterval = 100;  // 传感器读取间隔，100ms

// 创建OneButton对象
OneButton button1(KEY1, true); // KEY1按钮，参数true表示按下时为LOW电平
OneButton button2(KEY2, true); // KEY2按钮，参数true表示按下时为LOW电平
OneButton button3(KEY3, true); // KEY3按钮，参数true表示按下时为LOW电平

bool fanState = false;      // 风扇的当前状态
bool pumpState = false;     // 水泵的当前状态

// 报警状态管理
bool fireAlarmActive = false;     // 火灾报警状态
bool gasAlarmActive = false;      // 煤气报警状态
unsigned long alarmStartTime = 0; // 报警开始时间
const unsigned long alarmDisplayTime = 2000; // 报警显示时间(2秒)

// 指纹模块状态
bool enrollingFinger = false;     // 正在注册指纹
bool deletingFinger = false;      // 正在删除指纹
unsigned long fingerOpStartTime = 0; // 指纹操作开始时间
const unsigned long fingerOpTimeout = 10000; // 指纹操作超时时间(10秒)

// 指纹操作反馈提示
bool showFeedback = false;           // 是否显示反馈
char feedbackMessage[50] = "";       // 反馈消息
unsigned long feedbackStartTime = 0;  // 反馈开始时间
const unsigned long feedbackDisplayTime = 2000; // 反馈显示时间(2秒)

// FPM383C指纹模块命令数组
uint8_t PS_GetImageBuffer[12] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x01,0x00,0x05};
uint8_t PS_GetChar1Buffer[13] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x01,0x00,0x08};
uint8_t PS_GetChar2Buffer[13] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x02,0x00,0x09};
uint8_t PS_AutoEnrollBuffer[17] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x08,0x31,'\0','\0',0x04,0x00,0x16,'\0','\0'};
uint8_t PS_DeleteBuffer[16] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x07,0x0C,'\0','\0',0x00,0x01,'\0','\0'};
uint8_t PS_SearchMBBuffer[17] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x08,0x04,0x01,0x00,0x00,0xFF,0xFF,0x02,0x0C};
uint8_t PS_EmptyBuffer[12] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x0D,0x00,0x11};
uint8_t PS_CancelBuffer[12] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x30,0x00,0x34};
uint8_t PS_ReceiveBuffer[20]; // 接收数据缓冲区

// WiFi连接相关变量
const char* ssid = "WLAN";           // WiFi名称
const char* password = "12346578";       // WiFi密码
bool wifiConnected = false;              // WiFi连接状态
unsigned long lastWiFiCheckTime = 0;     // 上次WiFi检查时间
const unsigned long wifiCheckInterval = 5000;  // WiFi检查间隔时间(5秒)
int wifiSignalStrength = 0;              // WiFi信号强度(RSSI值)
bool showingWiFiPage = false;            // 是否显示WiFi页面

//----------------------------------------
// 函数声明
//----------------------------------------
void displayData(float temperature, float humidity, float lux, int flameValue, int mq2Value, int dB);
void handleButton();
void handleButton3();
void readSensors(float &temperature, float &humidity, float &lux, int &flameValue, int &mq2Value, int &dB);
void displayFingerPage(); // 显示指纹管理页面
void addFinger();  // 添加指纹功能
void deleteFinger(); // 删除指纹功能
void displayAlarm(const char* message); // 显示报警信息
void displayFeedback(); // 显示操作反馈

// 按钮回调函数
void toggleLight(); // 切换灯的状态
void switchPage(); // 切换页面和模式
void toggleFan(); // 切换风扇的状态
void nextFingerId(); // 切换下一个指纹ID
void previousFingerId(); // 切换上一个指纹ID
void confirmFingerOperation(); // 确认指纹操作

// 指纹模块相关函数
void FPM383C_SendData(int len, uint8_t PS_Databuffer[]); // 发送数据到指纹模块
void FPM383C_ReceiveData(uint16_t Timeout); // 从指纹模块接收数据
uint8_t PS_GetImage(); // 获取指纹图像
uint8_t PS_GetChar1(); // 生成特征存到缓冲区1
uint8_t PS_AutoEnroll(uint16_t PageID); // 自动注册指纹
uint8_t PS_Delete(uint16_t PageID); // 删除指纹
uint8_t PS_Empty(); // 清空所有指纹
uint8_t PS_Cancel(); // 取消当前操作

// WiFi相关函数
void connectToWiFi();                   // 连接WiFi
void checkWiFiStatus();                 // 检查WiFi状态
void displayWiFiPage();                 // 显示WiFi状态页面

//----------------------------------------
// 初始化设置
void setup()
{
  // 初始化串口通信
  Serial.begin(115200);  // 初始化串口用于调试
  
  // 初始化OLED显示屏
  u8g2.begin(); 
  u8g2.enableUTF8Print();  // 启用UTF8打印，支持中文显示

  // 初始化DHT11温湿度传感器
  dht.begin();

  // 初始化I2C总线，指定SDA和SCL引脚
  Wire1.begin(15, 41);  // SDA=15, SCL=41

  // 初始化BH1750光照传感器
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire1);

  // 初始化指纹模块串口
  mySerial.begin(57600);

  // 设置输入引脚
  pinMode(FLAME_SENSOR_PIN, INPUT);  // 火焰传感器
  pinMode(MQ2_SENSOR_PIN, INPUT);    // MQ-2气体传感器
  pinMode(KEY1, INPUT);              // 按键1
  pinMode(KEY2, INPUT);              // 按键2
  pinMode(KEY3, INPUT);              // 按键3
  pinMode(VOICE, INPUT);             // max4466语音传感器
  pinMode(ZW_IRQ, INPUT);            // 指纹模块检测引脚设置为输入
  // 设置输出引脚
  pinMode(LIGHT_PIN, OUTPUT);        // LED灯
  pinMode(FAN_PIN, OUTPUT);          // 风扇
  pinMode(PUMP_PIN, OUTPUT);         // 水泵
  pinMode(ZW_CTRL, OUTPUT);          // 指纹模块启动引脚设置为输出
  
  // 初始化输出设备状态
  digitalWrite(PUMP_PIN, LOW);       // 水泵初始状态为关闭
  
  // 配置按钮事件回调
  button1.attachClick(toggleLight); // 短按按钮1切换灯的状态
  button2.attachClick(toggleFan);   // 短按按钮2切换风扇的状态
  
  // 使用k3按钮控制指纹功能
  button3.attachClick(nextFingerId); // 短按按钮3切换指纹ID
  button3.attachDoubleClick(confirmFingerOperation); // 双击按钮3执行指纹操作
  button3.attachLongPressStart(switchPage); // 长按按钮3切换页面/模式
  
  // 设置按钮长按检测时间（单位：毫秒）
  button3.setPressTicks(1500);
  
  // 增加按键灵敏度设置
  button1.setClickTicks(50);  // 减少点击判定时间为50ms（默认是400ms）
  button2.setClickTicks(50);  // 减少点击判定时间
  button3.setClickTicks(200);  // 设置较长的点击判定时间，以便更容易区分双击
  
  // 设置防抖动时间
  button1.setDebounceTicks(10); // 减少防抖时间为10ms（默认是50ms）
  button2.setDebounceTicks(10); // 减少防抖时间
  button3.setDebounceTicks(20); // 增加防抖时间以提高双击检测稳定性
  
  // 初始化WiFi连接
  connectToWiFi();
}

//----------------------------------------
// 主循环
//----------------------------------------
void loop()
{
  // 获取当前时间
  unsigned long currentTime = millis();
  
  // 检测按钮状态（高频率）
  button1.tick();
  button2.tick();
  button3.tick();
  
  // 检测指纹模块检测引脚状态，控制指纹模块启动引脚输出
  if (digitalRead(ZW_IRQ) == HIGH) {
    digitalWrite(ZW_CTRL, HIGH);
  } else {
    digitalWrite(ZW_CTRL, HIGH);
  }
  
  // 检查WiFi状态（定期检查）
  if (currentTime - lastWiFiCheckTime >= wifiCheckInterval) {
    checkWiFiStatus();
    lastWiFiCheckTime = currentTime;
  }
  
  // 显示反馈信息（优先级最高）
  if (showFeedback) {
    displayFeedback();
    
    // 检查是否需要关闭反馈显示
    if (currentTime - feedbackStartTime >= feedbackDisplayTime) {
      showFeedback = false;
    }
    
    // 在显示反馈时不执行其他显示操作
    delay(10);
    return;
  }
  
  // 根据当前页面显示不同内容
  if (currentPage == 0) {
    // 传感器数据变量
    static float temperature = 0, humidity = 0, lux = 0;
    static int flameValue = 0, mq2Value = 0, dB = 0;
    
    // 控制传感器读取频率
    if (currentTime - lastSensorReadTime >= sensorReadInterval) {
      // 读取所有传感器数据
      readSensors(temperature, humidity, lux, flameValue, mq2Value, dB);
      
      // 检查DHT11数据是否有效
      if (isnan(humidity) || isnan(temperature))
      {
        // 如果读取失败，显示错误信息
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.setCursor(0, 15);
        u8g2.print("DHT11 Error");
        u8g2.sendBuffer();
        delay(1000); // 延迟1秒后重试
        lastSensorReadTime = currentTime; // 更新上次读取时间
        return;
      }
      
      // 火灾检测 - 火焰值大于2000自动打开水泵
      if (flameValue > 2000 && !fireAlarmActive) {
        // 打开水泵
        digitalWrite(PUMP_PIN, HIGH);
        pumpState = true;
        
        // 设置报警状态和开始时间
        fireAlarmActive = true;
        alarmStartTime = currentTime;
        
        // 显示火灾报警信息
        displayAlarm("检测到发生火灾！！！\n已打开灭火设备");
      }
      
      // 煤气泄漏检测 - MQ-2值大于2000自动打开风扇
      if (mq2Value > 2000 && !gasAlarmActive) {
        // 打开风扇
        digitalWrite(FAN_PIN, HIGH);
        fanState = true;
        
        // 设置报警状态和开始时间
        gasAlarmActive = true;
        alarmStartTime = currentTime;
        
        // 显示煤气泄漏报警信息
        displayAlarm("检测到煤气泄漏！！！\n已打开风扇");
      }
      
      lastSensorReadTime = currentTime; // 更新上次读取时间
    }
    
    // 检查报警显示时间是否结束
    if ((fireAlarmActive || gasAlarmActive) && currentTime - alarmStartTime >= alarmDisplayTime) {
      // 重置报警状态
      fireAlarmActive = false;
      gasAlarmActive = false;
    }
    
    // 只有在没有报警显示时才显示正常传感器数据
    if (!fireAlarmActive && !gasAlarmActive) {
      // 显示所有传感器数据
      displayData(temperature, humidity, lux, flameValue, mq2Value, dB);
    }
  } else if (currentPage == 1) {
    // 显示添加指纹页面
    displayFingerPage();
    
    // 根据当前状态处理指纹操作
    if (enrollingFinger) {
      // 处理添加指纹
      addFinger();
    }
  } else if (currentPage == 2) {
    // 显示删除指纹页面
    displayFingerPage();
    
    // 处理删除指纹
    if (deletingFinger) {
      deleteFinger();
    }
  } else if (currentPage == 3) {
    // 显示WiFi连接状态页面
    displayWiFiPage();
  }
  
  // 短暂延迟，减少CPU占用但保持按键灵敏度
  delay(10);
}

//----------------------------------------
// 显示报警信息
//----------------------------------------
void displayAlarm(const char* message)
{
  // 清空OLED显示屏缓冲区
  u8g2.clearBuffer();
  
  // 使用中文字体显示报警信息
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  
  // 将消息分行显示
  char buffer[100];
  strcpy(buffer, message);
  
  char* line = strtok(buffer, "\n");
  int y = 25;
  
  while (line != NULL) {
    u8g2.setCursor(0, y);
    u8g2.print(line);
    line = strtok(NULL, "\n");
    y += 20;
  }
  
  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();
}

//----------------------------------------
// 读取所有传感器数据
//----------------------------------------
void readSensors(float &temperature, float &humidity, float &lux, int &flameValue, int &mq2Value, int &dB)
{
  // 读取火焰传感器的模拟值
  flameValue = 4095 - analogRead(FLAME_SENSOR_PIN);

  // 读取MQ-2传感器的模拟值
  mq2Value = analogRead(MQ2_SENSOR_PIN);

  // 读取max4466语音传感器
  dB = analogRead(VOICE); 

  // 读取DHT11的温湿度数据
  humidity = dht.readHumidity();       // 读取湿度
  temperature = dht.readTemperature(); // 读取温度（摄氏度）

  // 读取BH1750的光照强度
  lux = lightMeter.readLightLevel();
}

//----------------------------------------
// 切换灯状态回调函数
//----------------------------------------
void toggleLight() {
  // 切换灯的状态
  lightState = !lightState;
  // 根据灯状态控制灯亮灭
  digitalWrite(LIGHT_PIN, lightState ? HIGH : LOW);
}

//----------------------------------------
// 切换风扇状态回调函数
//----------------------------------------
void toggleFan() {
  // 切换风扇的状态
  fanState = !fanState;
  // 根据风扇状态控制风扇开关
  digitalWrite(FAN_PIN, fanState ? HIGH : LOW);
}

//----------------------------------------
// 切换页面和模式回调函数
//----------------------------------------
void switchPage() {
  // 修改逻辑：增加WiFi页面，在四个页面间循环切换
  if (currentPage == 0) {
    // 从主页面切换到添加指纹页面
    currentPage = 1;
    
    // 重置指纹相关状态
    fingerOption = 0; // 设置为添加模式
    fingerId = 1;     // 重置指纹ID为1
    
    // 切换页面时显示提示信息
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setCursor(0, 35);
    u8g2.print("切换到添加指纹");
    u8g2.sendBuffer();
    delay(1000); // 显示1秒切换提示
  } else if (currentPage == 1) {
    // 从添加指纹页面切换到删除指纹页面
    currentPage = 2;
    
    // 设置为删除模式
    fingerOption = 1;
    fingerId = 1;     // 重置指纹ID为1
    
    // 切换页面时显示提示信息
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setCursor(0, 35);
    u8g2.print("切换到删除指纹");
    u8g2.sendBuffer();
    delay(1000); // 显示1秒切换提示
  } else if (currentPage == 2) {
    // 从删除指纹页面切换到WiFi页面
    currentPage = 3;
    
    // 切换页面时显示提示信息
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setCursor(0, 35);
    u8g2.print("切换到WiFi页面");
    u8g2.sendBuffer();
    delay(1000); // 显示1秒切换提示
  } else {
    // 从WiFi页面返回主页面
    currentPage = 0;
    
    // 重置指纹相关状态
    fingerOption = 0; // 重置指纹选项
    fingerId = 1;     // 重置指纹ID为1
    enrollingFinger = false; // 取消任何正在进行的指纹操作
    deletingFinger = false;
    showFeedback = false;    // 取消任何反馈显示
    
    // 切换页面时显示提示信息
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setCursor(0, 35);
    u8g2.print("切换到主页面");
    u8g2.sendBuffer();
    delay(1000); // 显示1秒切换提示
  }
}

//----------------------------------------
// 显示指纹管理页面
//----------------------------------------
void displayFingerPage()
{
  // 清空OLED显示屏缓冲区
  u8g2.clearBuffer();

  // 显示标题
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(32, 14);
  u8g2.print("指纹管理");

  // 显示操作提示
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  
  // 显示当前模式标题
  u8g2.setCursor(0, 30);
  if (currentPage == 1) {
    u8g2.print("添加指纹模式");
  } else if (currentPage == 2) {
    u8g2.print("删除指纹模式");
  }
  
  // 显示选择的指纹ID
  u8g2.setCursor(0, 46);
  u8g2.print("ID: ");
  
  // 显示6个ID，当前选择的ID高亮显示
  for (int i = 1; i <= MAX_FINGER_ID; i++) {
    if (i == fingerId) {
      u8g2.print("[");
      u8g2.print(i);
      u8g2.print("]");
    } else {
      u8g2.print(" ");
      u8g2.print(i);
      u8g2.print(" ");
    }
  }
  
  // 显示操作说明
  u8g2.setCursor(0, 62);
  if (enrollingFinger || deletingFinger) {
    u8g2.print("正在处理，请稍候...");
  } else {
    u8g2.print("短按:切换ID 双击:执行");
  }

  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();
}

//----------------------------------------
// 添加指纹功能
//----------------------------------------
void addFinger()
{
  // 仅在添加模式活跃时执行
  if (!enrollingFinger) return;

  // 检查是否超时
  if (millis() - fingerOpStartTime > fingerOpTimeout) {
    // 超时，取消操作
    enrollingFinger = false;
    strcpy(feedbackMessage, "添加指纹超时");
    showFeedback = true;
    feedbackStartTime = millis();
    return;
  }

  // 检测指纹模块是否唤醒（通过ZW_IRQ引脚）
  if (digitalRead(ZW_IRQ) == HIGH) {
    // 指纹模块已唤醒，开始添加指纹
    digitalWrite(ZW_CTRL, HIGH); // 激活指纹模块
    
    // 执行添加指纹操作
    uint8_t result = PS_AutoEnroll(fingerId - 1); // 注意ID从0开始，界面从1开始
    
    // 处理添加结果
    enrollingFinger = false;
    if (result == 0x00) {
      // 添加成功
      strcpy(feedbackMessage, "指纹添加成功");
    } else {
      // 添加失败
      strcpy(feedbackMessage, "指纹添加失败");
    }
    
    // 显示反馈信息
    showFeedback = true;
    feedbackStartTime = millis();
  }
}

//----------------------------------------
// 删除指纹功能
//----------------------------------------
void deleteFinger()
{
  // 仅在删除模式活跃时执行
  if (!deletingFinger) return;

  // 检查是否超时
  if (millis() - fingerOpStartTime > fingerOpTimeout) {
    // 超时，取消操作
    deletingFinger = false;
    strcpy(feedbackMessage, "删除指纹超时");
    showFeedback = true;
    feedbackStartTime = millis();
    return;
  }

  // 执行删除指纹操作
  uint8_t result = PS_Delete(fingerId - 1); // 注意ID从0开始，界面从1开始
  
  // 处理删除结果
  deletingFinger = false;
  if (result == 0x00) {
    // 删除成功
    strcpy(feedbackMessage, "指纹删除成功");
  } else {
    // 删除失败
    strcpy(feedbackMessage, "指纹删除失败");
  }
  
  // 显示反馈信息
  showFeedback = true;
  feedbackStartTime = millis();
}

//----------------------------------------
// 在OLED上显示所有数据
//----------------------------------------
void displayData(float temperature, float humidity, float lux, int flameValue, int mq2Value, int dB)
{
  // 清空OLED显示屏缓冲区
  u8g2.clearBuffer();

  // 显示标题（第一行）- 使用中文字体
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(12, 14);
  u8g2.print("智能舍管助手");

  // 切换到英文字体显示数据
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  // 显示火焰传感器和MQ-2传感器值（第二行）
  u8g2.setCursor(0, 30);
  u8g2.print("Flame: ");
  u8g2.print(flameValue);
  u8g2.print(" MQ-2: ");
  u8g2.print(mq2Value);

  // 显示光照强度和分贝值（第三行）
  u8g2.setCursor(0, 45);
  u8g2.print("L: ");
  u8g2.print(lux);
  u8g2.print("lx");
  u8g2.print("  dB: ");
  u8g2.print(dB);

  // 显示温湿度值（第四行）
  u8g2.setCursor(0, 60);
  u8g2.print("T: ");
  u8g2.print(temperature);
  u8g2.print("C    ");
  u8g2.print("H: ");
  u8g2.print(humidity);
  u8g2.print("%");

  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();
}

//----------------------------------------
// 指纹模块相关函数实现
//----------------------------------------

/**
 * 发送数据到指纹模块
 */
void FPM383C_SendData(int len, uint8_t PS_Databuffer[])
{
  mySerial.write(PS_Databuffer, len);
  mySerial.flush();
}

/**
 * 从指纹模块接收数据
 */
void FPM383C_ReceiveData(uint16_t Timeout)
{
  uint8_t i = 0;
  while(mySerial.available() == 0 && (--Timeout))
  {
    delay(1);
  }
  while(mySerial.available() > 0)
  {
    delay(2);
    PS_ReceiveBuffer[i++] = mySerial.read();
    if(i > 15) break; 
  }
}

/**
 * 获取指纹图像
 */
uint8_t PS_GetImage()
{
  FPM383C_SendData(12, PS_GetImageBuffer);
  FPM383C_ReceiveData(2000);
  return PS_ReceiveBuffer[6] == 0x07 ? PS_ReceiveBuffer[9] : 0xFF;
}

/**
 * 生成特征存到缓冲区1
 */
uint8_t PS_GetChar1()
{
  FPM383C_SendData(13, PS_GetChar1Buffer);
  FPM383C_ReceiveData(2000);
  return PS_ReceiveBuffer[6] == 0x07 ? PS_ReceiveBuffer[9] : 0xFF;
}

/**
 * 取消当前操作
 */
uint8_t PS_Cancel()
{
  FPM383C_SendData(12, PS_CancelBuffer);
  FPM383C_ReceiveData(2000);
  return PS_ReceiveBuffer[6] == 0x07 ? PS_ReceiveBuffer[9] : 0xFF;
}

/**
 * 自动注册指纹
 */
uint8_t PS_AutoEnroll(uint16_t PageID)
{
  PS_AutoEnrollBuffer[10] = (PageID>>8);
  PS_AutoEnrollBuffer[11] = (PageID);
  PS_AutoEnrollBuffer[15] = (0x54+PS_AutoEnrollBuffer[10]+PS_AutoEnrollBuffer[11])>>8;
  PS_AutoEnrollBuffer[16] = (0x54+PS_AutoEnrollBuffer[10]+PS_AutoEnrollBuffer[11]);
  FPM383C_SendData(17, PS_AutoEnrollBuffer);
  FPM383C_ReceiveData(10000);
  return PS_ReceiveBuffer[6] == 0x07 ? PS_ReceiveBuffer[9] : 0xFF;
}

/**
 * 删除指纹
 */
uint8_t PS_Delete(uint16_t PageID)
{
  PS_DeleteBuffer[10] = (PageID>>8);
  PS_DeleteBuffer[11] = (PageID);
  PS_DeleteBuffer[14] = (0x15+PS_DeleteBuffer[10]+PS_DeleteBuffer[11])>>8;
  PS_DeleteBuffer[15] = (0x15+PS_DeleteBuffer[10]+PS_DeleteBuffer[11]);
  FPM383C_SendData(16, PS_DeleteBuffer);
  FPM383C_ReceiveData(2000);
  return PS_ReceiveBuffer[6] == 0x07 ? PS_ReceiveBuffer[9] : 0xFF;
}

/**
 * 清空所有指纹
 */
uint8_t PS_Empty()
{
  FPM383C_SendData(12, PS_EmptyBuffer);
  FPM383C_ReceiveData(2000);
  return PS_ReceiveBuffer[6] == 0x07 ? PS_ReceiveBuffer[9] : 0xFF;
}

//----------------------------------------
// 显示操作反馈
//----------------------------------------
void displayFeedback()
{
  // 清空OLED显示屏缓冲区
  u8g2.clearBuffer();
  
  // 使用中文字体显示反馈信息
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  
  // 计算文本居中位置
  int y = 35;
  
  u8g2.setCursor(0, y);
  u8g2.print(feedbackMessage);
  
  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();
}

//----------------------------------------
// 切换下一个指纹ID回调函数
//----------------------------------------
void nextFingerId() {
  // 仅在指纹管理页面生效
  if ((currentPage == 1 || currentPage == 2) && !enrollingFinger && !deletingFinger) {
    // 切换到下一个ID，循环切换
    fingerId = (fingerId % MAX_FINGER_ID) + 1;
  }
}

//----------------------------------------
// 切换上一个指纹ID回调函数
//----------------------------------------
void previousFingerId() {
  // 仅在指纹管理页面生效
  if ((currentPage == 1 || currentPage == 2) && !enrollingFinger && !deletingFinger) {
    // 切换到上一个ID，循环切换
    fingerId = (fingerId > 1) ? (fingerId - 1) : MAX_FINGER_ID;
  }
}

//----------------------------------------
// 确认指纹操作回调函数
//----------------------------------------
void confirmFingerOperation() {
  // 仅在指纹管理页面生效
  if ((currentPage == 1 || currentPage == 2) && !enrollingFinger && !deletingFinger) {
    if (currentPage == 1) {
      // 在添加指纹页面，开始添加指纹
      enrollingFinger = true;
      fingerOpStartTime = millis();
      // 更新页面提示正在处理
      displayFingerPage();
    } else if (currentPage == 2) {
      // 在删除指纹页面，开始删除指纹
      deletingFinger = true;
      fingerOpStartTime = millis();
      // 更新页面提示正在处理
      displayFingerPage();
    }
  }
}

//----------------------------------------
// WiFi相关函数实现
//----------------------------------------

/**
 * 连接WiFi网络
 */
void connectToWiFi() {
  // 显示正在连接WiFi的信息
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(0, 30);
  u8g2.print("正在连接WiFi...");
  u8g2.sendBuffer();
  
  // 开始WiFi连接
  WiFi.begin(ssid, password);
  
  // 等待连接，最多等待10秒
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    // 可以在这里添加一些连接过程的动画显示
  }
  
  // 判断连接结果
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiSignalStrength = WiFi.RSSI();
    
    // 显示连接成功信息
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setCursor(0, 30);
    u8g2.print("WiFi连接成功");
    u8g2.setCursor(0, 50);
    u8g2.print("IP: ");
    u8g2.print(WiFi.localIP().toString().c_str());
    u8g2.sendBuffer();
    delay(2000);
  } else {
    wifiConnected = false;
    
    // 显示连接失败信息
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setCursor(0, 30);
    u8g2.print("WiFi连接失败");
    u8g2.setCursor(0, 50);
    u8g2.print("请检查网络");
    u8g2.sendBuffer();
    delay(2000);
  }
}

/**
 * 检查WiFi连接状态
 */
void checkWiFiStatus() {
  // 检查当前WiFi状态
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiSignalStrength = WiFi.RSSI(); // 更新信号强度
  } else {
    wifiConnected = false;
    
    // 尝试重新连接WiFi（如果之前连接成功但现在断开）
    if (wifiConnected) {
      // 重置WiFi连接
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
    }
  }
}

/**
 * 显示WiFi连接状态页面
 */
void displayWiFiPage() {
  // 清空OLED显示屏缓冲区
  u8g2.clearBuffer();
  
  // 显示标题
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(16, 14);
  u8g2.print("WiFi状态");
  
  // 显示WiFi状态信息
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  
  // 显示连接状态
  u8g2.setCursor(0, 28);
  u8g2.print("状态: ");
  if (wifiConnected) {
    u8g2.print("已连接");
  } else {
    u8g2.print("未连接");
  }
  
  // 显示WiFi名称
  u8g2.setCursor(0, 40);
  u8g2.print("SSID: ");
  u8g2.print(ssid);
  
  // 如果已连接，显示IP地址和信号强度
  if (wifiConnected) {
    // 显示IP地址
    u8g2.setCursor(0, 52);
    u8g2.print("IP: ");
    u8g2.print(WiFi.localIP().toString().c_str());
    
    // 显示信号强度
    u8g2.setCursor(0, 64);
    u8g2.print("信号: ");
    u8g2.print(wifiSignalStrength);
    u8g2.print("dBm");
  } else {
    // 未连接时显示尝试重连的提示
    u8g2.setCursor(0, 52);
    u8g2.print("正在尝试重连...");
  }
  
  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();
}

