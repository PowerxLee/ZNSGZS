#include <Arduino.h>
#include <U8g2lib.h>  // OLED显示屏库
#include <DHT.h>      // DHT11温湿度传感器库
#include <DHT_U.h>    // DHT11扩展库
#include <Wire.h>     // I2C通信库
#include <BH1750.h>   // BH1750光照传感器库
#include <OneButton.h> // 按键处理库
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

//----------------------------------------
// 全局变量
//----------------------------------------
bool lightState = false;     // LED灯的当前状态
bool lastButtonState = HIGH; // 按键的上一次状态
bool lastButton3State = HIGH; // 按键3的上一次状态
unsigned long button3PressTime = 0; // 按键3按下的时间
bool button3LongPress = false; // 按键3长按标志

int currentPage = 0;         // 当前页面编号，0为主页面，1为指纹管理页面
int fingerOption = 0;        // 指纹选项，0为添加指纹，1为删除指纹

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

// 按钮回调函数
void toggleLight(); // 切换灯的状态
void switchPage(); // 切换页面
void toggleFingerOption(); // 切换指纹选项
void toggleFan(); // 切换风扇的状态

//----------------------------------------
// 初始化设置
void setup()
{
  // 初始化OLED显示屏
  u8g2.begin(); 
  u8g2.enableUTF8Print();  // 启用UTF8打印，支持中文显示

  // 初始化DHT11温湿度传感器
  dht.begin();

  // 初始化I2C总线，指定SDA和SCL引脚
  Wire1.begin(15, 41);  // SDA=15, SCL=41

  // 初始化BH1750光照传感器
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire1);

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
  button3.attachLongPressStart(switchPage); // 长按按钮3切换页面
  button3.attachClick(toggleFingerOption); // 短按按钮3切换指纹选项
  
  // 设置按钮长按检测时间（单位：毫秒）
  button3.setPressTicks(1500);
  
  // 增加按键灵敏度设置
  button1.setClickTicks(50);  // 减少点击判定时间为50ms（默认是400ms）
  button2.setClickTicks(50);  // 减少点击判定时间
  button3.setClickTicks(50);  // 同样减少点击判定时间
  
  // 设置防抖动时间
  button1.setDebounceTicks(10); // 减少防抖时间为10ms（默认是50ms）
  button2.setDebounceTicks(10); // 减少防抖时间
  button3.setDebounceTicks(10); // 同样减少防抖时间
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
    digitalWrite(ZW_CTRL, LOW);
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
    // 显示指纹管理页面
    displayFingerPage();
    
    // 根据选项执行相应功能
    if (fingerOption == 0) {
      // 添加指纹功能接口
      addFinger();
    } else {
      // 删除指纹功能接口
      deleteFinger();
    }
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
// 切换页面回调函数
//----------------------------------------
void switchPage() {
  // 切换页面
  currentPage = (currentPage == 0) ? 1 : 0;
  fingerOption = 0; // 重置指纹选项
  
  // 切换页面时显示提示信息
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(0, 35);
  if (currentPage == 0) {
    u8g2.print("切换到主页面");
  } else {
    u8g2.print("切换到指纹管理");
  }
  u8g2.sendBuffer();
  delay(1000); // 显示1秒切换提示
}

//----------------------------------------
// 切换指纹选项回调函数
//----------------------------------------
void toggleFingerOption() {
  // 仅在指纹管理页面生效
  if (currentPage == 1) {
    // 切换功能选项
    fingerOption = (fingerOption == 0) ? 1 : 0;
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

  // 显示功能选项
  u8g2.setCursor(0, 38);
  u8g2.print(fingerOption == 0 ? "→ 添加指纹" : "  添加指纹");
  
  u8g2.setCursor(0, 60);
  u8g2.print(fingerOption == 1 ? "→ 删除指纹" : "  删除指纹");

  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();
}

//----------------------------------------
// 添加指纹功能入口
//----------------------------------------
void addFinger()
{
  // 此处只实现功能入口，后续可扩展完整功能
  // 可以在这里添加与指纹模块通信的代码
}

//----------------------------------------
// 删除指纹功能入口
//----------------------------------------
void deleteFinger()
{
  // 此处只实现功能入口，后续可扩展完整功能
  // 可以在这里添加与指纹模块通信的代码
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

