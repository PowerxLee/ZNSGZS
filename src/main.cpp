#include <Arduino.h>
#include <U8g2lib.h>  // OLED显示屏库
#include <DHT.h>      // DHT11温湿度传感器库
#include <DHT_U.h>    // DHT11扩展库
#include <Wire.h>     // I2C通信库
#include <BH1750.h>   // BH1750光照传感器库
#include <OneButton.h> // 按键处理库
#include "SoftwareSerial.h"       //注意添加这个软串口头文件s
#include<WiFi.h>
#include <PubSubClient.h> // MQTT库
#include <ArduinoJson.h>  // JSON库
#include <Ticker.h>      // 定时器库
#include "ClosedCube_SHT31D.h"

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
#define BUZZER_PIN 42       // 蜂鸣器引脚

// 输入设备引脚
#define KEY1 47             // 按键引脚
#define KEY2 38             // 按键引脚
#define KEY3 39             // 按键3引脚
//----------------------------------------
// 阿里云MQTT配置
//----------------------------------------
#define PRODUCT_KEY       "a1kyhW4QQ1t"       // 替换为你的阿里云PRODUCT_KEY
#define DEVICE_NAME       "ZNSGZS"            // 替换为你的阿里云DEVICE_NAME
#define DEVICE_SECRET     "c15db7d20d58754ca996b7d2352a583d" // 替换为你的阿里云DEVICE_SECRET
#define REGION_ID         "cn-shanghai"

/* 线上环境域名和端口号 */
#define MQTT_SERVER       PRODUCT_KEY".iot-as-mqtt."REGION_ID ".aliyuncs.com"
#define MQTT_PORT         1883
#define MQTT_USRNAME      DEVICE_NAME"&"PRODUCT_KEY

// 这里需要根据阿里云生成的三元组信息修改
#define CLIENT_ID         "a1kyhW4QQ1t.ZNSGZS|securemode=2,signmethod=hmacsha256,timestamp=1747019835103|"
#define MQTT_PASSWD       "ae0dbce0965f1974fafd10eb9da3834db870f6d1d2ca27d19ba08ca502224942" // 根据阿里云规则生成的密码

// 阿里云主题定义
#define ALI_TOPIC_PROP_POST     "/sys/" PRODUCT_KEY "/" DEVICE_NAME "/thing/event/property/post"
#define ALI_TOPIC_PROP_SET      "/sys/" PRODUCT_KEY "/" DEVICE_NAME "/thing/service/property/set"
#define ALI_TOPIC_PROP_POST_REPLY "/sys/" PRODUCT_KEY "/" DEVICE_NAME "/thing/event/property/post_reply"
#define ALI_TOPIC_PROP_FORMAT   "{\"id\":\"%u\",\"version\":\"1.0\",\"method\":\"thing.event.property.post\",\"params\":%s}"

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

// MQTT相关变量
int postMsgId = 0; // 消息ID,每次上报属性时递增
WiFiClient espClient; // 创建WiFiClient对象
PubSubClient mqttClient(espClient); // 创建PubSubClient对象
Ticker mqttTicker; // 创建定时器对象，用于定时上报数据

//----------------------------------------
// 全局变量
//----------------------------------------
bool lightState = false;     // LED灯的当前状态
bool lastButtonState = HIGH; // 按键的上一次状态
bool lastButton3State = HIGH; // 按键3的上一次状态
unsigned long button3PressTime = 0; // 按键3按下的时间
bool button3LongPress = false; // 按键3长按标志

// 传感器阈值变量
float temperatureThreshold = 30.0;   // 温度阈值（摄氏度）
float humidityThreshold = 80.0;      // 湿度阈值（百分比）
float lightThreshold = 30000.0;        // 亮度阈值（勒克斯）
int decibelThreshold = 80;           // 分贝阈值（dB）
int flameThreshold = 50;             // 火焰阈值（0-100）
int smokeThreshold = 60;             // 烟雾阈值（0-100）

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
bool fanManualControl = false;  // 风扇手动控制标志
bool pumpManualControl = false; // 水泵手动控制标志

// 报警状态管理
bool fireAlarmActive = false;     // 火灾报警状态
bool smokeAlarmActive = false;      // 烟雾报警状态

// 查寝功能相关变量
bool checkInModeActive = false;          // 查寝模式激活状态
unsigned long checkInStartTime = 0;      // 查寝开始时间
const unsigned long checkInDuration = 60000; // 查寝持续时间（1分钟）
bool fingerCheckedIn[7] = {false}; // 指纹打卡状态数组(下标0不使用，最大支持6个ID)
int userCheckInStatus = 0;               // 查寝状态（0:未开始,1:进行中,2:已完成）
bool lastCheckInFlag = false;            // 上次查寝标志状态，用于检测变化
bool lastResetCheckInFlag = false;       // 上次重置查寝标志状态，用于检测变化

// 蜂鸣器报警状态管理
bool buzzerActive = false;         // 蜂鸣器激活状态
unsigned long lastBuzzerToggleTime = 0;  // 上次蜂鸣器状态切换时间
unsigned long buzzerToggleInterval = 500; // 蜂鸣器状态切换间隔（毫秒）
bool buzzerState = false;          // 蜂鸣器当前状态（高/低）

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
const char* ssid = "12345";           // WiFi名称
const char* password = "00000000";       // WiFi密码
bool wifiConnected = false;              // WiFi连接状态
unsigned long lastWiFiCheckTime = 0;     // 上次WiFi检查时间
const unsigned long wifiCheckInterval = 5000;  // WiFi检查间隔时间(5秒)
int wifiSignalStrength = 0;              // WiFi信号强度(RSSI值)
bool showingWiFiPage = false;            // 是否显示WiFi页面

// MQTT连接状态和数据上传间隔
bool mqtt_connected = false;
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long mqttReconnectInterval = 5000;  // 重连间隔5秒
unsigned long lastDataUploadTime = 0;
const unsigned long dataUploadInterval = 10000;    // 每10秒上传一次数据

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
void displayFeedback(); // 显示操作反馈
void startCheckInMode(); // 开始查寝模式
void handleCheckInMode(); // 处理查寝模式
void resetCheckInStatus(); // 重置查寝状态
void reportCheckInResult(); // 上报查寝结果
void displayCheckInPage(); // 显示查寝页面

// 按钮回调函数
void toggleLight(); // 切换灯的状态
void switchPage(); // 切换页面和模式
void toggleFan(); // 切换风扇的状态
void togglePump(); // 切换水泵状态
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

// MQTT相关函数
void connectToAliyun();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishSensorData();

//----------------------------------------
// 蜂鸣器控制函数
//----------------------------------------
void handleBuzzer() {
  // 如果存在报警事件，激活蜂鸣器
  if (fireAlarmActive || smokeAlarmActive) {
    buzzerActive = true;
  } else {
    // 如果所有报警解除，停止蜂鸣器
    buzzerActive = false;
    buzzerState = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }
  
  // 如果蜂鸣器激活，处理高低交替报警
  if (buzzerActive) {
    unsigned long currentTime = millis();
    
    // 检查是否需要切换蜂鸣器状态
    if (currentTime - lastBuzzerToggleTime >= buzzerToggleInterval) {
      // 切换蜂鸣器状态 (高/低)
      buzzerState = !buzzerState;
      // 更新蜂鸣器输出（注意是高电平触发）
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      // 更新上次切换时间
      lastBuzzerToggleTime = currentTime;
    }
  }
}

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
  pinMode(BUZZER_PIN, OUTPUT);       // 蜂鸣器引脚设置为输出
  
  // 初始化输出设备状态
  digitalWrite(PUMP_PIN, LOW);       // 水泵初始状态为关闭
  digitalWrite(BUZZER_PIN, LOW);     // 蜂鸣器初始状态为关闭
  
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
  
  // 初始化MQTT客户端
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  
  // 连接WiFi后连接OneNET
  if (wifiConnected) {
    connectToAliyun();
    // 设置10秒定时上报数据
    mqttTicker.attach(1, publishSensorData);
    
    // 初始化发送absentUsers默认值
    char statusBuffer[128];
    sprintf(statusBuffer, "{\"id\":\"%u\",\"version\":\"1.0\",\"method\":\"thing.event.property.post\",\"params\":{\"absentUsers\":\"未开启查寝\"}}", postMsgId++);
    mqttClient.publish(ALI_TOPIC_PROP_POST, statusBuffer);
  }
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
  
  // 查寝模式处理（优先级第二）
  if (checkInModeActive) {
    handleCheckInMode();
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
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.setCursor(0, 15);
        u8g2.print("DHT11 Error");
        u8g2.sendBuffer();
        delay(1000); // 延迟1秒后重试
        lastSensorReadTime = currentTime; // 更新上次读取时间
        return;
      }
      
      // 温度检测 - 温度大于阈值自动打开风扇
      if (temperature > temperatureThreshold) {
        // 打开风扇
        digitalWrite(FAN_PIN, HIGH);
        fanState = true;
        fanManualControl = false; // 自动控制模式
      } else{
        // 只有在非手动控制模式下才自动关闭风扇
        if (!fanManualControl) {
          digitalWrite(FAN_PIN, LOW);
          fanState = false;
        }
      }
      
      // 湿度检测 - 湿度大于阈值可以添加相应操作
      if (humidity > humidityThreshold) {
        // 这里可以添加湿度过高时的操作，例如打开风扇或其他设备
      }
      
      // 火灾检测 - 火焰值大于阈值自动打开水泵
      if (flameValue > flameThreshold) {
        // 打开水泵
        digitalWrite(PUMP_PIN, HIGH);
        pumpState = true;
        pumpManualControl = false; // 自动控制模式
        // 设置火灾报警状态
        fireAlarmActive = true;
      } else {
        // 火灾解除
        fireAlarmActive = false;
        // 只有在非手动控制模式下才自动关闭水泵
        if (!pumpManualControl) {
          digitalWrite(PUMP_PIN, LOW);
          pumpState = false;
        }
      }
      
      // 烟雾泄漏检测 - MQ-2值大于阈值自动打开风扇
      if (mq2Value > smokeThreshold) {
        // 打开风扇
        digitalWrite(FAN_PIN, HIGH);
        fanState = true;
        fanManualControl = false; // 自动控制模式
        // 设置烟雾泄漏报警状态
        smokeAlarmActive = true;
      } else {
        // 烟雾泄漏解除
        smokeAlarmActive = false;
        // 只有在非手动控制模式下才自动关闭风扇
        if (!fanManualControl) {
          digitalWrite(FAN_PIN, LOW);
          fanState = false;
        }
      }
      
      lastSensorReadTime = currentTime; // 更新上次读取时间
    }
    
    // 处理蜂鸣器报警
    handleBuzzer();
    
    // 显示所有传感器数据
    displayData(temperature, humidity, lux, flameValue, mq2Value, dB);
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
  }
  
  // MQTT连接维护
  if (wifiConnected && !mqttClient.connected()) {
    connectToAliyun();
  }
  
  // 处理MQTT消息
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  
  // 短暂延迟，减少CPU占用但保持按键灵敏度
  delay(10);
}

//----------------------------------------
// 读取所有传感器数据
//----------------------------------------
void readSensors(float &temperature, float &humidity, float &lux, int &flameValue, int &mq2Value, int &dB)
{
  // 读取火焰传感器的模拟值
  int rawFlameValue = 4095 - analogRead(FLAME_SENSOR_PIN);
  // 映射火焰传感器值到0-100范围
  flameValue = map(rawFlameValue, 0, 4095, 0, 100);
  
  // 读取MQ-2传感器的模拟值
  int rawMq2Value = analogRead(MQ2_SENSOR_PIN);
  // 映射MQ-2传感器值到0-100范围
  mq2Value = map(rawMq2Value, 0, 4095, 0, 100);
  
  // 读取max4466语音传感器
  int rawDbValue = analogRead(VOICE);
  // 映射语音传感器值到0-100范围
  dB = map(rawDbValue, 0, 4095, 0, 100);

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
  // 切换风扇的状态和手动控制标志
  fanState = !fanState;
  fanManualControl = fanState; // 如果开启则设为手动控制，如果关闭则取消手动控制
  
  // 根据风扇状态控制风扇开关
  digitalWrite(FAN_PIN, fanState ? HIGH : LOW);
}

//----------------------------------------
// 切换水泵状态函数
//----------------------------------------
void togglePump() {
  // 切换水泵的状态和手动控制标志
  pumpState = !pumpState;
  pumpManualControl = pumpState; // 如果开启则设为手动控制，如果关闭则取消手动控制
  
  // 根据水泵状态控制水泵开关
  digitalWrite(PUMP_PIN, pumpState ? HIGH : LOW);
}

//----------------------------------------
// 切换页面和模式回调函数
//----------------------------------------
void switchPage() {
  // 修改逻辑：在三个页面间循环切换（去掉WiFi页面）
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
  } else {
    // 从删除指纹页面返回主页面
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
  
  // 在标题行右侧显示WiFi图标
  if (wifiConnected) {
    // 使用符号字体绘制WiFi图标
    u8g2.setFont(u8g2_font_siji_t_6x10);
    u8g2.drawGlyph(110, 12, 0x0e21a); // WiFi图标的Unicode值
  }

  // 切换到英文字体显示数据
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  // 显示火焰传感器和MQ-2传感器值（第二行）
  u8g2.setCursor(0, 30);
  u8g2.print("Flame: ");
  u8g2.print(flameValue);
  u8g2.print("%  MQ-2: ");
  u8g2.print(mq2Value);
  u8g2.print("%");

  // 显示光照强度和分贝值（第三行）
  u8g2.setCursor(0, 45);
  u8g2.print("L: ");
  u8g2.print(lux);
  u8g2.print("lx");
  u8g2.print("  dB: ");
  u8g2.print(dB);
  u8g2.print("dB");

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
// 连接阿里云物联网平台
//----------------------------------------
void connectToAliyun() {
  if (!wifiConnected) return;  // 如果WiFi未连接，不尝试连接阿里云
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(0, 35);
  u8g2.print("连接阿里云中...");
  u8g2.sendBuffer();
  
  // 尝试连接阿里云，重试5次
  int retryCount = 0;
  while (!mqttClient.connected() && retryCount < 5) {
    Serial.println("连接阿里云MQTT服务器...");
    
    if (mqttClient.connect(CLIENT_ID, MQTT_USRNAME, MQTT_PASSWD)) {
      Serial.println("成功连接到阿里云!");
      
      // 成功连接后订阅主题
      mqttClient.subscribe(ALI_TOPIC_PROP_SET);
      mqttClient.subscribe(ALI_TOPIC_PROP_POST_REPLY);
      
      u8g2.clearBuffer();
      u8g2.setCursor(0, 35);
      u8g2.print("阿里云连接成功");
      u8g2.sendBuffer();
      delay(1000);
      
      break;
    } else {
      retryCount++;
      Serial.print("连接失败，错误码：");
      Serial.println(mqttClient.state());
      Serial.println("3秒后重试...");
      delay(3000);
    }
  }
  
  if (!mqttClient.connected()) {
    u8g2.clearBuffer();
    u8g2.setCursor(0, 35);
    u8g2.print("阿里云连接失败");
    u8g2.sendBuffer();
    delay(1000);
  }
}

//----------------------------------------
// MQTT回调函数-处理收到的消息
//----------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("收到消息 [");
  Serial.print(topic);
  Serial.print("] ");
  
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
    Serial.print((char)payload[i]);
  }
  message[length] = '\0';
  Serial.println();
  
  // 处理属性设置请求 - 阿里云平台
  if (strcmp(topic, ALI_TOPIC_PROP_SET) == 0) {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print("解析JSON失败: ");
      Serial.println(error.c_str());
      return;
    }
    
    // 提取消息ID
    String msgId = doc["id"];
    
    // 阿里云平台参数格式
    if (doc.containsKey("params")) {
      JsonObject params = doc["params"];
      
      // 处理查寝启动属性
      if (params.containsKey("startCheckIn")) {
        bool checkInFlag = (int)params["startCheckIn"] == 1;
        
        // 只在属性从0变为1时触发查寝（避免重复触发）
        if (checkInFlag && !lastCheckInFlag && !checkInModeActive) {
          startCheckInMode();
          
          // 上报正在查寝状态
          char statusBuffer[128];
          sprintf(statusBuffer, "{\"id\":\"%u\",\"version\":\"1.0\",\"method\":\"thing.event.property.post\",\"params\":{\"absentUsers\":\"正在查寝，请稍等...\"}}", postMsgId++);
          mqttClient.publish(ALI_TOPIC_PROP_POST, statusBuffer);
        }
        
        // 更新上次状态
        lastCheckInFlag = checkInFlag;
      }
      
      // 处理查寝重置属性
      if (params.containsKey("resetCheckIn")) {
        bool resetFlag = (int)params["resetCheckIn"] == 1;
        
        // 只在属性从0变为1时重置查寝状态
        if (resetFlag && !lastResetCheckInFlag) {
          // 重置查寝状态
          checkInModeActive = false;
          userCheckInStatus = 0;
          
          // 上报重置查寝状态
          char statusBuffer[128];
          sprintf(statusBuffer, "{\"id\":\"%u\",\"version\":\"1.0\",\"method\":\"thing.event.property.post\",\"params\":{\"absentUsers\":\"未开启查寝\",\"resetCheckIn\":0}}", postMsgId++);
          mqttClient.publish(ALI_TOPIC_PROP_POST, statusBuffer);
          
          Serial.println("查寝状态已重置");
        }
        
        // 更新上次状态
        lastResetCheckInFlag = resetFlag;
      }
      
      // 处理收到的属性设置
      if (params.containsKey("lightState")) {
        lightState = (int)params["lightState"] == 1;
        digitalWrite(LIGHT_PIN, lightState ? HIGH : LOW);
        Serial.print("设置灯光状态: ");
        Serial.println(lightState);
      }
      
      if (params.containsKey("fanState")) {
        fanState = (int)params["fanState"] == 1;
        fanManualControl = true; // 设为手动控制模式
        digitalWrite(FAN_PIN, fanState ? HIGH : LOW);
        Serial.print("设置风扇状态: ");
        Serial.println(fanState);
      }
      
      if (params.containsKey("pumpState")) {
        pumpState = (int)params["pumpState"] == 1;
        pumpManualControl = true; // 设为手动控制模式
        digitalWrite(PUMP_PIN, pumpState ? HIGH : LOW);
        Serial.print("设置水泵状态: ");
        Serial.println(pumpState);
      }
      
      // 处理阈值设置
      if (params.containsKey("temperatureThreshold")) {
        temperatureThreshold = params["temperatureThreshold"];
        Serial.print("设置温度阈值: ");
        Serial.println(temperatureThreshold);
      }
      
      if (params.containsKey("humidityThreshold")) {
        humidityThreshold = params["humidityThreshold"];
        Serial.print("设置湿度阈值: ");
        Serial.println(humidityThreshold);
      }
      
      if (params.containsKey("lightThreshold")) {
        lightThreshold = params["lightThreshold"];
        Serial.print("设置亮度阈值: ");
        Serial.println(lightThreshold);
      }
      
      if (params.containsKey("decibelThreshold")) {
        decibelThreshold = params["decibelThreshold"];
        Serial.print("设置分贝阈值: ");
        Serial.println(decibelThreshold);
      }
      
      if (params.containsKey("flameThreshold")) {
        flameThreshold = params["flameThreshold"];
        Serial.print("设置火焰阈值: ");
        Serial.println(flameThreshold);
      }
      
      if (params.containsKey("smokeThreshold")) {
        smokeThreshold = params["smokeThreshold"];
        Serial.print("设置烟雾阈值: ");
        Serial.println(smokeThreshold);
      }
      
      // 发送属性设置响应
      char responseBuf[100];
      sprintf(responseBuf, "{\"id\":\"%s\",\"code\":200,\"data\":{}}", msgId.c_str());
      mqttClient.publish(ALI_TOPIC_PROP_POST_REPLY, responseBuf);
      Serial.println("已发送设置响应");
    }
  }
}

//----------------------------------------
// 发布传感器数据到阿里云
//----------------------------------------
void publishSensorData() {
  if (!wifiConnected || !mqttClient.connected()) return;
  
  // 读取传感器数据
  float temperature = 0, humidity = 0, lux = 0;
  int flameValue = 0, mq2Value = 0, dB = 0;
  readSensors(temperature, humidity, lux, flameValue, mq2Value, dB);
  
  // 检查DHT11数据是否有效
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("DHT11数据无效，跳过本次上报");
    return;
  }
  
  // 创建JSON数据
  char params[350]; // 增加缓冲区大小以适应更多数据
  char jsonBuf[600]; // 增加缓冲区大小以适应更多数据
  
  // 构建参数JSON，阿里云格式，添加阈值数据
  sprintf(params, "{"
    "\"temperature\":%.1f,"
    "\"humidity\":%.1f,"
    "\"light\":%.1f,"
    "\"flame\":%d,"
    "\"smoke\":%d,"
    "\"noise\":%d,"
    "\"lightState\":%d,"
    "\"fanState\":%d,"
    "\"pumpState\":%d,"
    "\"temperatureThreshold\":%.1f,"
    "\"humidityThreshold\":%.1f,"
    "\"lightThreshold\":%.1f,"
    "\"flameThreshold\":%d,"
    "\"smokeThreshold\":%d,"
    "\"decibelThreshold\":%d"
    "}",
    temperature, humidity, lux, 
    flameValue, mq2Value, dB,
    lightState ? 1 : 0,
    fanState ? 1 : 0,
    pumpState ? 1 : 0,
    temperatureThreshold, humidityThreshold, lightThreshold,
    flameThreshold, smokeThreshold, decibelThreshold
  );
  
  // 构建完整的JSON消息
  sprintf(jsonBuf, ALI_TOPIC_PROP_FORMAT, postMsgId++, params);
  
  // 发布到阿里云
  if (mqttClient.publish(ALI_TOPIC_PROP_POST, jsonBuf)) {
    Serial.println("传感器数据上报成功");
  } else {
    Serial.println("传感器数据上报失败");
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
    
    // WiFi连接成功后，连接OneNET
    connectToAliyun();
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
  
  // 如果WiFi连接状态发生变化，更新MQTT连接
  static bool lastWifiState = false;
  if (wifiConnected != lastWifiState) {
    lastWifiState = wifiConnected;
    
    if (wifiConnected) {
      connectToAliyun();
    }
  }
}

//----------------------------------------
// 启动查寝模式
//----------------------------------------
void startCheckInMode() {
  // 设置查寝模式激活
  checkInModeActive = true;
  userCheckInStatus = 1; // 设置为查寝进行中
  checkInStartTime = millis(); // 记录查寝开始时间
  
  // 重置所有指纹打卡状态
  for (int i = 0; i <= MAX_FINGER_ID; i++) {
    fingerCheckedIn[i] = false;
  }
  
  // 显示提示信息
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(0, 35);
  u8g2.print("查寝开始");
  u8g2.setCursor(0, 55);
  u8g2.print("请所有人指纹打卡");
  u8g2.sendBuffer();
  
  // 启动蜂鸣器提示一声
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  
  Serial.println("查寝模式已启动");
}

//----------------------------------------
// 处理查寝模式
//----------------------------------------
void handleCheckInMode() {
  // 获取当前时间
  unsigned long currentTime = millis();
  
  // 检查是否到达查寝时间限制或全员已打卡
  bool allCheckedIn = true;
  for (int i = 1; i <= MAX_FINGER_ID; i++) {
    if (!fingerCheckedIn[i]) {
      allCheckedIn = false;
      break;
    }
  }
  
  if (currentTime - checkInStartTime >= checkInDuration || allCheckedIn) {
    // 查寝时间结束或全员已打卡
    userCheckInStatus = 2; // 设置为查寝已完成
    
    // 显示结束原因
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setCursor(0, 35);
    u8g2.print("查寝结束");
    u8g2.setCursor(0, 55);
    if (allCheckedIn) {
      u8g2.print("全员已打卡");
    } else {
      u8g2.print("时间已到");
    }
    u8g2.sendBuffer();
    
    // 发出蜂鸣器提示音
    if (allCheckedIn) {
      // 全员打卡成功，发出快速三声提示
      for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(100);
      }
    } else {
      // 时间到，发出两声提示
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      delay(200);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
    }
    
    // 上报查寝结果
    reportCheckInResult();
    
    checkInModeActive = false;
    return;
  }
  
  // 查寝过程中，检测是否有指纹验证
  if (digitalRead(ZW_IRQ) == HIGH) {
    digitalWrite(ZW_CTRL, HIGH); // 激活指纹模块
    
    // 获取指纹图像
    if (PS_GetImage() == 0x00) {
      // 生成特征并存入缓冲区1
      if (PS_GetChar1() == 0x00) {
        // 搜索指纹
        FPM383C_SendData(17, PS_SearchMBBuffer);
        FPM383C_ReceiveData(5000);
        
        // 如果是成功匹配的结果
        if (PS_ReceiveBuffer[9] == 0x00) {
          // 获取匹配的指纹ID（注意这里的偏移可能需要根据实际指纹模块协议调整）
          int matchedId = PS_ReceiveBuffer[10] * 256 + PS_ReceiveBuffer[11] + 1; // +1是因为界面ID从1开始
          
          // 检查ID是否在有效范围内
          if (matchedId >= 1 && matchedId <= MAX_FINGER_ID) {
            // 将该指纹ID标记为已打卡
            fingerCheckedIn[matchedId] = true;
            
            // 显示打卡成功信息
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_wqy16_t_gb2312);
            u8g2.setCursor(0, 35);
            u8g2.print("ID");
            u8g2.print(matchedId);
            u8g2.print("打卡成功");
            u8g2.sendBuffer();
            
            // 蜂鸣器提示一声
            digitalWrite(BUZZER_PIN, HIGH);
            delay(100);
            digitalWrite(BUZZER_PIN, LOW);
            
            // 延迟一秒
            delay(1000);
            
            // 检查是否全员已打卡
            bool allCheckedIn = true;
            for (int i = 1; i <= MAX_FINGER_ID; i++) {
              if (!fingerCheckedIn[i]) {
                allCheckedIn = false;
                break;
              }
            }
            
            // 如果全员已打卡，可以立即结束查寝（这里不直接结束，让下一个循环检测到并结束）
          }
        }
      }
    }
  }
  
  // 显示查寝页面，包括倒计时和已打卡状态
  displayCheckInPage();
}

//----------------------------------------
// 显示查寝页面
//----------------------------------------
void displayCheckInPage() {
  // 获取当前时间
  unsigned long currentTime = millis();
  
  // 计算剩余时间（秒）
  int remainingSeconds = (checkInDuration - (currentTime - checkInStartTime)) / 1000;
  if (remainingSeconds < 0) remainingSeconds = 0;
  
  // 计算未打卡人数
  int absentCount = 0;
  for (int i = 1; i <= MAX_FINGER_ID; i++) {
    if (!fingerCheckedIn[i]) {
      absentCount++;
    }
  }
  
  // 清空OLED显示屏缓冲区
  u8g2.clearBuffer();
  
  // 显示标题
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(32, 14);
  u8g2.print("查寝中");
  
  // 显示倒计时
  u8g2.setCursor(0, 30);
  u8g2.print("倒计时: ");
  u8g2.print(remainingSeconds);
  u8g2.print("秒");
  
  // 显示未打卡人数
  u8g2.setCursor(0, 46);
  u8g2.print("未打卡: ");
  u8g2.print(absentCount);
  u8g2.print("人");
  
  // 仅显示未打卡的ID
  if (absentCount > 0) {
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setCursor(0, 58);
    u8g2.print("未打卡ID: ");
    
    int xPos = 60; // 起始x位置
    for (int i = 1; i <= MAX_FINGER_ID; i++) {
      if (!fingerCheckedIn[i]) {
        // 判断是否需要换行
        if (xPos > 110) {
          break; // 如果超出屏幕宽度，不再显示更多ID
        }
        
        // 显示未打卡的ID
        u8g2.setCursor(xPos, 58);
        u8g2.print(i);
        xPos += 12; // 每个ID占12像素宽
      }
    }
  } else {
    // 全部已打卡
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setCursor(0, 58);
    u8g2.print("全部已打卡");
  }
  
  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();
}

//----------------------------------------
// 上报查寝结果
//----------------------------------------
void reportCheckInResult() {
  if (!wifiConnected || !mqttClient.connected()) {
    Serial.println("网络未连接，无法上报查寝结果");
    return;
  }
  
  // 创建JSON对象
  DynamicJsonDocument doc(512);
  
  // 设置消息基本信息
  doc["id"] = String(postMsgId++);
  doc["version"] = "1.0";
  doc["method"] = "thing.event.property.post";
  
  // 创建params对象
  JsonObject params = doc.createNestedObject("params");
  
  // 构造未打卡人员ID字符串
  String absentString = "";
  bool isFirst = true;
  bool hasAbsent = false;
  
  // 统计并添加未打卡的用户ID
  for (int i = 1; i <= MAX_FINGER_ID; i++) {
    if (!fingerCheckedIn[i]) {
      if (!isFirst) {
        absentString += ","; // 添加分隔符
      }
      absentString += String(i); // 添加ID
      isFirst = false;
      hasAbsent = true;
    }
  }
  
  // 如果没有未打卡人员，显示"全员已打卡"
  if (!hasAbsent) {
    absentString = "全员已打卡";
  }
  
  // 设置未打卡人员字符串
  params["absentUsers"] = absentString;
  
  // 重置查寝启动属性
  params["startCheckIn"] = 0;
  
  // 序列化JSON
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  
  // 发布到阿里云属性上报主题
  if (mqttClient.publish(ALI_TOPIC_PROP_POST, jsonBuffer)) {
    Serial.println("查寝结果上报成功");
    // 重置本地状态
    lastCheckInFlag = false;
  } else {
    Serial.println("查寝结果上报失败");
  }
}


