#include <Arduino.h>
#include <U8g2lib.h>  // OLED显示屏库
#include <Wire.h>     // I2C通信库

//----------------------------------------
// 引脚定义
//----------------------------------------
// 传感器引脚
#define FLAME_SENSOR_PIN 2  // 火焰传感器引脚
#define MQ2_SENSOR_PIN 8    // MQ-2气体传感器引脚
#define BUZZER_PIN 42       // 蜂鸣器引脚
#define SHT30_SDA_PIN 3     // SHT30 SDA引脚
#define SHT30_SCL_PIN 4     // SHT30 SCL引脚
#define SHT30_ADDR 0x44     // SHT30 I2C地址
#define OLED_SCL_PIN 21     // OLED SCL引脚
#define OLED_SDA_PIN 40     // OLED SDA引脚

//----------------------------------------
// 全局对象初始化
//----------------------------------------
// 初始化OLED显示屏 从硬件I2C改为软件I2C
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/OLED_SCL_PIN, /* data=*/OLED_SDA_PIN, /* reset=*/U8X8_PIN_NONE);

//----------------------------------------
// 全局变量
//----------------------------------------
// 传感器阈值变量
int flameThreshold = 50;             // 火焰阈值（0-100）
int smokeThreshold = 60;             // 烟雾阈值（0-100）
float tempThreshold = 40.0;          // 温度阈值（摄氏度）
float humidityThreshold = 80.0;      // 湿度阈值（百分比）

// 添加时间管理变量
unsigned long lastSensorReadTime = 0;  // 上次传感器读取时间
const unsigned long sensorReadInterval = 100;  // 传感器读取间隔，100ms

// 报警状态管理
bool fireAlarmActive = false;     // 火灾报警状态
bool smokeAlarmActive = false;    // 烟雾报警状态
bool tempAlarmActive = false;     // 温度报警状态
bool humidityAlarmActive = false; // 湿度报警状态

// 蜂鸣器报警状态管理
bool buzzerActive = false;         // 蜂鸣器激活状态
unsigned long lastBuzzerToggleTime = 0;  // 上次蜂鸣器状态切换时间
unsigned long buzzerToggleInterval = 500; // 蜂鸣器状态切换间隔（毫秒）
bool buzzerState = false;          // 蜂鸣器当前状态（高/低）

//----------------------------------------
// 函数声明
//----------------------------------------
void displayData(int flameValue, int mq2Value, float temperature, float humidity);
void readSensors(int &flameValue, int &mq2Value, float &temperature, float &humidity);
bool readSHT30(float &temperature, float &humidity);

//----------------------------------------
// 蜂鸣器控制函数
//----------------------------------------
void handleBuzzer() {
  // 如果存在报警事件，激活蜂鸣器
  if (fireAlarmActive || smokeAlarmActive || tempAlarmActive || humidityAlarmActive) {
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
// SHT30温湿度传感器读取函数
//----------------------------------------
bool readSHT30(float &temperature, float &humidity) {
  unsigned int data[6];
  
  // 开始I2C通信
  Wire.beginTransmission(SHT30_ADDR);
  // 发送测量命令 0x2C06
  Wire.write(0x2C);
  Wire.write(0x06);
  // 结束传输
  Wire.endTransmission();
  
  // 延时等待SHT30完成测量
  delay(50);
  
  // 请求6字节的数据
  if (Wire.requestFrom(SHT30_ADDR, 6) != 6) {
    return false;  // 如果没有收到6个字节，返回错误
  }
  
  // 读取6字节数据
  // 温度高8位、低8位、CRC校验
  // 湿度高8位、低8位、CRC校验
  for (int i = 0; i < 6; i++) {
    data[i] = Wire.read();
  }
  
  // 计算温度和湿度
  temperature = ((((data[0] * 256.0) + data[1]) * 175) / 65535.0) - 45;
  humidity = ((((data[3] * 256.0) + data[4]) * 100) / 65535.0);
  
  return true;
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

  // 初始化用于SHT30的I2C总线
  Wire.begin(SHT30_SDA_PIN, SHT30_SCL_PIN);
  
  // 测试SHT30传感器
  float temp, humi;
  if (!readSHT30(temp, humi)) {
    Serial.println("找不到SHT30传感器");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setCursor(0, 35);
    u8g2.print("SHT30初始化失败");
    u8g2.sendBuffer();
    delay(2000);
  } else {
    Serial.println("SHT30传感器初始化成功");
    Serial.print("温度: ");
    Serial.print(temp);
    Serial.print("°C, 湿度: ");
    Serial.print(humi);
    Serial.println("%");
  }

  // 设置输入引脚
  pinMode(FLAME_SENSOR_PIN, INPUT);  // 火焰传感器
  pinMode(MQ2_SENSOR_PIN, INPUT);    // MQ-2气体传感器
  
  // 设置输出引脚
  pinMode(BUZZER_PIN, OUTPUT);       // 蜂鸣器引脚设置为输出
  
  // 初始化输出设备状态
  digitalWrite(BUZZER_PIN, LOW);     // 蜂鸣器初始状态为关闭
}

//----------------------------------------
// 主循环
//----------------------------------------
void loop()
{
  // 获取当前时间
  unsigned long currentTime = millis();
  
  // 传感器数据变量
  static int flameValue = 0, mq2Value = 0;
  static float temperature = 0, humidity = 0;
  
  // 控制传感器读取频率
  if (currentTime - lastSensorReadTime >= sensorReadInterval) {
    // 读取所有传感器数据
    readSensors(flameValue, mq2Value, temperature, humidity);
    
    // 火灾检测 - 火焰值大于阈值触发报警
    if (flameValue > flameThreshold) {
      // 设置火灾报警状态
      fireAlarmActive = true;
    } else {
      // 火灾解除
      fireAlarmActive = false;
    }
    
    // 烟雾泄漏检测 - MQ-2值大于阈值触发报警
    if (mq2Value > smokeThreshold) {
      // 设置烟雾泄漏报警状态
      smokeAlarmActive = true;
    } else {
      // 烟雾泄漏解除
      smokeAlarmActive = false;
    }
    
    // 温度报警检测
    if (temperature > tempThreshold) {
      tempAlarmActive = true;
    } else {
      tempAlarmActive = false;
    }
    
    // 湿度报警检测
    if (humidity > humidityThreshold) {
      humidityAlarmActive = true;
    } else {
      humidityAlarmActive = false;
    }
    
    lastSensorReadTime = currentTime; // 更新上次读取时间
  }
  
  // 处理蜂鸣器报警
  handleBuzzer();
  
  // 显示传感器数据
  displayData(flameValue, mq2Value, temperature, humidity);
  
  // 短暂延迟，减少CPU占用
  delay(10);
}

//----------------------------------------
// 读取传感器数据
//----------------------------------------
void readSensors(int &flameValue, int &mq2Value, float &temperature, float &humidity)
{
  // 读取火焰传感器的模拟值
  int rawFlameValue = 4095 - analogRead(FLAME_SENSOR_PIN);
  // 映射火焰传感器值到0-100范围
  flameValue = map(rawFlameValue, 0, 4095, 0, 100);
  
  // 读取MQ-2传感器的模拟值
  int rawMq2Value = analogRead(MQ2_SENSOR_PIN);
  // 映射MQ-2传感器值到0-100范围
  mq2Value = map(rawMq2Value, 0, 4095, 0, 100);
  
  // 读取SHT30温湿度数据
  if (!readSHT30(temperature, humidity)) {
    Serial.println("无法读取SHT30温度数据");
    temperature = 0;
    humidity = 0;
  }
}

//----------------------------------------
// 在OLED上显示数据
//----------------------------------------
void displayData(int flameValue, int mq2Value, float temperature, float humidity)
{
  // 清空OLED显示屏缓冲区
  u8g2.clearBuffer();

  // 显示标题（第一行）- 使用中文字体
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(12, 14);
  u8g2.print("环境监测系统");

  // 切换到英文字体显示数据
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  // 显示火焰传感器和MQ-2传感器值（第二行）
  u8g2.setCursor(0, 30);
  u8g2.print("Flame: ");
  u8g2.print(flameValue);
  u8g2.print("%  MQ-2: ");
  u8g2.print(mq2Value);
  u8g2.print("%");
  
  // 显示温湿度数据（第三行）
  u8g2.setCursor(0, 45);
  u8g2.print("Temp: ");
  u8g2.print(temperature);
  u8g2.print("C  Humi: ");
  u8g2.print(humidity);
  u8g2.print("%");

  // 显示报警状态（第四行）
  u8g2.setCursor(0, 60);
  if (fireAlarmActive || smokeAlarmActive || tempAlarmActive || humidityAlarmActive) {
    if (fireAlarmActive) {
      u8g2.print("Fire Alert!");
    } else if (smokeAlarmActive) {
      u8g2.print("Smoke Alert!");
    } else if (tempAlarmActive) {
      u8g2.print("Temp Alert!");
    } else if (humidityAlarmActive) {
      u8g2.print("Humidity Alert!");
    }
  } else {
    u8g2.print("Normal");
  }
  
  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();
}


