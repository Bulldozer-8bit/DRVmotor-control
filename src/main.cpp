#include <Arduino.h>
#include <Wire.h>

#define DRV2605_ADDR 0x5A
#define I2C_SDA 8
#define I2C_SCL 9

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(DRV2605_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // 初始化：退出待机，设置 LRA 模式
  writeReg(0x01, 0x00); // Mode: Internal Trigger
  writeReg(0x1A, 0xB6); // Feedback: LRA Mode
  writeReg(0x03, 0x06); // Library: LRA Library
}

void loop() {
  Serial.println("正在播放：单次点击（类似按钮按下）");
  
  // 填充波形序列器 (寄存器 0x04 到 0x0B 可填 8 个波形)
  writeReg(0x04, 1);   // 波形 1: Triple Click
  writeReg(0x05, 0);    // 结束标志
  
  writeReg(0x0C, 0x01); // GO!
  
  delay(2000);

  Serial.println("正在播放：重低音震动（类似背心爆炸效果）");
  writeReg(0x04, 1);    // Strong Click
  writeReg(0x05, 1);    // 再来一个
  writeReg(0x06, 47);   // 强蜂鸣长震动
  writeReg(0x07, 0);    // 结束
  
  writeReg(0x0C, 0x01); // GO!
  
  delay(3000);
}