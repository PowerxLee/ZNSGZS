#include <Arduino.h>
#include <U8g2lib.h> // OLED库
#include <DHT.h>     // DHT11库
#include <DHT_U.h>   // DHT11库
#include <Wire.h>    // I2C通信库
#include <BH1750.h>  // BH1750光照传感器库

// 定义火焰传感器引脚
#define FLAME_SENSOR_PIN 2

// 定义MQ-2传感器引脚
#define MQ2_SENSOR_PIN 8

// 定义DHT11引脚和类型
#define DHTPIN 1      // DHT11数据引脚连接到GPIO 1
#define DHTTYPE DHT11 // 传感器类型为DHT11

// 定义灯引脚
#define LIGHT_PIN 16

// 初始化U8G2库，使用I2C接口
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/38, /* data=*/47);

// 初始化DHT11
DHT dht(DHTPIN, DHTTYPE);

// 初始化BH1750
BH1750 lightMeter(0x23);
void setup()
{
  // 初始化OLED显示屏
  u8g2.begin();
  u8g2.enableUTF8Print();

  // 初始化DHT11
  dht.begin();

  // 初始化I2C总线，指定SDA和SCL引脚
  Wire1.begin(15, 41); // SDA=15, SCL=41

  // 初始化BH1750
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire1);

  // 设置火焰传感器引脚为输入
  pinMode(FLAME_SENSOR_PIN, INPUT);

  // 设置MQ-2传感器引脚为输入
  pinMode(MQ2_SENSOR_PIN, INPUT);

  // 设置灯引脚为输出
  pinMode(LIGHT_PIN, OUTPUT);

}
void loop()
{
  // 读取火焰传感器的模拟值
  int flameValue = 4095 - analogRead(FLAME_SENSOR_PIN);

  // 读取MQ-2传感器的模拟值
  int mq2Value = analogRead(MQ2_SENSOR_PIN);

  // 模拟分贝值
  int dB = 99; 

  // 读取DHT11的温湿度数据
  float humidity = dht.readHumidity();       // 读取湿度
  float temperature = dht.readTemperature(); // 读取温度（摄氏度）

  // 读取BH1750的光照强度
  float lux = lightMeter.readLightLevel();
  //控制灯亮灭  HIGH 为亮，LOW 为灭
  digitalWrite(LIGHT_PIN, LOW);

  // 检查DHT11数据是否有效
  if (isnan(humidity) || isnan(temperature))
  {
    // 如果读取失败，显示错误信息
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setCursor(0, 15);
    u8g2.print("DHT11 Error");
    u8g2.sendBuffer();
    delay(2000); // 延迟2秒后重试
    return;
  }

  // 清空OLED显示屏缓冲区
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_wqy16_t_gb2312); // 使用中文字体
  // 显示标题（第一行）
  u8g2.setCursor(12, 14);
  u8g2.print("智能舍管助手");

  u8g2.setFont(u8g2_font_ncenB08_tr);
  // 显示火焰传感器和MQ-2传感器值（第二行）
  u8g2.setCursor(0, 30);
  u8g2.print("Flame: ");
  u8g2.print(flameValue);
  u8g2.print(" MQ-2: ");
  u8g2.print(mq2Value);

  // 显示光照强度值（第三行）
  u8g2.setCursor(0, 45);
  u8g2.print("L: ");
  u8g2.print(lux);
  u8g2.print("lx");

  // 显示分贝值（第三行）
  u8g2.print("  dB: ");
  u8g2.print(dB);

  // 显示温湿度值（第四行，格式为 T: <value>C   &H:<value>%）
  u8g2.setCursor(0, 60);
  u8g2.print("T: ");
  u8g2.print(temperature);
  u8g2.print("C    ");
  u8g2.print("H: ");
  u8g2.print(humidity);
  u8g2.print("%");

  // 发送缓冲区内容到OLED显示屏
  u8g2.sendBuffer();

  // 延迟一段时间
  delay(500); // 每0.5秒更新一次
}