#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_DRV2605.h"

Adafruit_DRV2605 drv;

void setup() {
  Serial.begin(115200);
  Serial.println("DRV2605 LRA Test");

  // 初始化 I2C，ESP32-S3 默认 SDA=4, SCL=5 (根据你的实际引脚修改)
  Wire.begin();

  if (!drv.begin()) {
    Serial.println("未能找到 DRV2605，请检查接线！");
    while (1);
  }

  // --- 关键配置：切换到 LRA 模式 ---
  drv.useLRA();             // 告诉芯片你接的是 LRA 而不是 ERM
  
  // 选择实时播放模式 (Real-time Playback)
  // 这种模式下，你可以通过代码精确控制振动的开启和关闭时间
  drv.setMode(DRV2605_MODE_REALTIME); 
}

/**
 * 按照特定频率振动
 * @param hz 目标频率 (例如 50Hz)
 * @param durationMs 持续总时间
 */
void vibrateAtFrequency(float hz, int durationMs) {
  int periodMicros = 1000000 / hz;  // 计算周期（微秒）
  int halfPeriod = periodMicros / 2;
  unsigned long startTime = millis();

  Serial.printf("正在以 %.2f Hz 频率振动...\n", hz);

  while (millis() - startTime < durationMs) {
    // 这里的 0x7F 是震动强度（0-127）
    drv.setRealtimeValue(0x7F); 
    delayMicroseconds(halfPeriod);
    
    drv.setRealtimeValue(0x00); 
    delayMicroseconds(halfPeriod);
  }
}

void loop() {
  // 示例：以 175Hz 振动 1 秒（很多 LRA 的额定谐振频率在 170-230Hz 之间）
  vibrateAtFrequency(175.0, 1000);
  
  delay(2000); // 停顿 2 秒

  // 示例：心跳感振动（高频短促两次）
  vibrateAtFrequency(200.0, 100);
  delay(100);
  vibrateAtFrequency(200.0, 100);
  
  delay(3000);
}