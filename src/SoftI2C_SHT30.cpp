#include "SoftI2C_SHT30.h"

// 基本I2C时序延迟（微秒）
#define I2C_DELAY_US 5

// 构造函数
SoftI2C_SHT30::SoftI2C_SHT30(uint8_t sda_pin, uint8_t scl_pin, uint8_t address) {
    _sda_pin = sda_pin;
    _scl_pin = scl_pin;
    _address = address;
}

// 初始化
void SoftI2C_SHT30::begin() {
    // 设置引脚模式
    pinMode(_sda_pin, OUTPUT);
    pinMode(_scl_pin, OUTPUT);
    
    // 初始状态：SDA和SCL均为高电平
    digitalWrite(_sda_pin, HIGH);
    digitalWrite(_scl_pin, HIGH);
}

// I2C总线延迟
void SoftI2C_SHT30::i2c_delay() {
    delayMicroseconds(I2C_DELAY_US);
}

// SDA设置为高电平（释放总线）
void SoftI2C_SHT30::sda_high() {
    pinMode(_sda_pin, INPUT_PULLUP); // 释放SDA线（相当于输出高电平）
    i2c_delay();
}

// SDA设置为低电平
void SoftI2C_SHT30::sda_low() {
    pinMode(_sda_pin, OUTPUT);
    digitalWrite(_sda_pin, LOW);
    i2c_delay();
}

// 读取SDA线状态
uint8_t SoftI2C_SHT30::sda_read() {
    return digitalRead(_sda_pin);
}

// SCL设置为高电平
void SoftI2C_SHT30::scl_high() {
    pinMode(_scl_pin, INPUT_PULLUP); // 释放SCL线（相当于输出高电平）
    i2c_delay();
    
    // 确保SCL真的变高（检测时钟拉伸）
    while(digitalRead(_scl_pin) == LOW) {
        // 等待SCL变高（有些从机可能拉低SCL进行时钟拉伸）
        delayMicroseconds(10);
    }
}

// SCL设置为低电平
void SoftI2C_SHT30::scl_low() {
    pinMode(_scl_pin, OUTPUT);
    digitalWrite(_scl_pin, LOW);
    i2c_delay();
}

// I2C起始条件
void SoftI2C_SHT30::i2c_start() {
    // 确保SDA和SCL都是高电平
    sda_high();
    scl_high();
    
    // 产生起始条件：SDA从高到低，而SCL保持高
    sda_low();
    i2c_delay();
    scl_low();
}

// I2C停止条件
void SoftI2C_SHT30::i2c_stop() {
    // 确保SDA是低电平，SCL是低电平
    sda_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    sda_high(); // SDA从低到高产生停止条件
    i2c_delay();
}

// 向I2C总线写入一个字节，返回是否收到ACK
bool SoftI2C_SHT30::i2c_write_byte(uint8_t byte) {
    // 发送8位数据
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) {
            sda_high();
        } else {
            sda_low();
        }
        scl_high();
        scl_low();
    }
    
    // 释放SDA线读取ACK
    sda_high(); // 释放SDA准备接收ACK
    scl_high(); // 时钟第9个周期
    
    // 读取ACK（低电平表示ACK）
    uint8_t ack = sda_read();
    
    scl_low();
    
    // 返回ACK状态（0=ACK，非0=NACK）
    return (ack == 0);
}

// 从I2C总线读取一个字节
uint8_t SoftI2C_SHT30::i2c_read_byte(bool ack) {
    uint8_t byte = 0;
    
    // 释放SDA线以便从机驱动数据
    sda_high();
    
    // 读取8位数据
    for (int i = 7; i >= 0; i--) {
        scl_high();
        if (sda_read()) {
            byte |= (1 << i);
        }
        scl_low();
    }
    
    // 发送ACK或NACK
    if (ack) {
        sda_low(); // ACK (拉低SDA)
    } else {
        sda_high(); // NACK (释放SDA)
    }
    
    scl_high();
    i2c_delay();
    scl_low();
    sda_high(); // 释放SDA线
    
    return byte;
}

// 发送命令给SHT30
bool SoftI2C_SHT30::sendCommand(uint16_t command) {
    i2c_start();
    
    // 发送地址 + 写入位 (0)
    bool ack = i2c_write_byte((_address << 1) | 0x00);
    if (!ack) {
        i2c_stop();
        return false;
    }
    
    // 发送命令高字节
    ack = i2c_write_byte(command >> 8);
    if (!ack) {
        i2c_stop();
        return false;
    }
    
    // 发送命令低字节
    ack = i2c_write_byte(command & 0xFF);
    if (!ack) {
        i2c_stop();
        return false;
    }
    
    i2c_stop();
    return true;
}

// CRC8校验，SHT30使用的多项式为x^8 + x^5 + x^4 + 1 = 0x31
bool SoftI2C_SHT30::checkCrc(uint8_t data[], uint8_t nbrOfBytes, uint8_t checksum) {
    uint8_t crc = 0xFF;
    uint8_t bit;
    
    // 计算CRC
    for (uint8_t byteCtr = 0; byteCtr < nbrOfBytes; byteCtr++) {
        crc ^= data[byteCtr];
        for (bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    
    // 验证校验和
    return (crc == checksum);
}

// 读取温湿度数据
SoftI2C_SHT30::SHT30_Result SoftI2C_SHT30::readTempAndHumidity() {
    SHT30_Result result = {0, 0, false}; // 初始化为无效结果
    uint8_t data[6]; // 接收6字节数据（温度2字节+CRC，湿度2字节+CRC）
    
    // 发送高精度测量命令
    if (!sendCommand(SHT30_COMMAND_MEASURE_HIGH_REP)) {
        return result; // 发送命令失败
    }
    
    // 等待转换完成
    delay(15); // SHT30高精度测量典型时间为12.5ms
    
    // 读取结果
    i2c_start();
    
    // 发送地址 + 读取位 (1)
    bool ack = i2c_write_byte((_address << 1) | 0x01);
    if (!ack) {
        i2c_stop();
        return result;
    }
    
    // 读取6字节数据
    data[0] = i2c_read_byte(true); // 温度高字节 + ACK
    data[1] = i2c_read_byte(true); // 温度低字节 + ACK
    data[2] = i2c_read_byte(true); // 温度CRC + ACK
    data[3] = i2c_read_byte(true); // 湿度高字节 + ACK
    data[4] = i2c_read_byte(true); // 湿度低字节 + ACK
    data[5] = i2c_read_byte(false); // 湿度CRC + NACK（读取最后一个字节后发送NACK）
    
    i2c_stop();
    
    // 验证CRC
    bool tempCrcOk = checkCrc(data, 2, data[2]);
    bool humidCrcOk = checkCrc(data + 3, 2, data[5]);
    
    if (tempCrcOk && humidCrcOk) {
        // 计算温度 (公式: T = -45 + 175 * rawValue / 65535)
        uint16_t rawTemp = ((uint16_t)data[0] << 8) | data[1];
        result.temperature = -45.0f + 175.0f * rawTemp / 65535.0f;
        
        // 计算湿度 (公式: RH = 100 * rawValue / 65535)
        uint16_t rawHumid = ((uint16_t)data[3] << 8) | data[4];
        result.humidity = 100.0f * rawHumid / 65535.0f;
        
        // 标记数据有效
        result.valid = true;
    }
    
    return result;
} 