#ifndef SOFTI2C_SHT30_H
#define SOFTI2C_SHT30_H

#include <Arduino.h>

class SoftI2C_SHT30 {
private:
    uint8_t _sda_pin;
    uint8_t _scl_pin;
    uint16_t _address;
    
    // SHT30命令
    static const uint16_t SHT30_COMMAND_MEASURE_HIGH_REP = 0x2400; // 高精度测量命令
    static const uint8_t SHT30_ADDRESS = 0x44; // SHT30默认地址 (0x44 或 0x45)

    // 软件I2C实现的基本函数
    void i2c_start();
    void i2c_stop();
    bool i2c_write_byte(uint8_t byte);
    uint8_t i2c_read_byte(bool ack);
    void i2c_delay();
    
    // SDA方向控制
    void sda_high();
    void sda_low();
    uint8_t sda_read();
    
    // SCL控制
    void scl_high();
    void scl_low();
    
    // CRC校验
    bool checkCrc(uint8_t data[], uint8_t nbrOfBytes, uint8_t checksum);

public:
    // 测量结果结构体
    struct SHT30_Result {
        float temperature;
        float humidity;
        bool valid;
    };
    
    // 构造函数
    SoftI2C_SHT30(uint8_t sda_pin, uint8_t scl_pin, uint8_t address = SHT30_ADDRESS);
    
    // 初始化
    void begin();
    
    // 读取传感器数据
    SHT30_Result readTempAndHumidity();
    
    // 向SHT30发送命令
    bool sendCommand(uint16_t command);
};

#endif // SOFTI2C_SHT30_H 