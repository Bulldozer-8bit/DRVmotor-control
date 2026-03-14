#include <Arduino.h>
#include <Wire.h>

// 基于你给出的寄存器映射的实现（粗略表）
// 重点使用：CHIPID (0x00), STATUS (0x01), RTP_INPUT (0x0E), GO (0x0C)
// 请在实际使用前核对下列寄存器地址和值是否与 DRV2506L 数据手册一致。

// ---- 硬件配置 ----
const uint8_t DRV_I2C_ADDR = 0x5A; // 请替换为实际 I2C 地址
const int FAULT_PIN = -1;          // 若要使用硬件故障检测，设为 GPIO，否则 -1

// I2C 引脚（显式指定，按你的开发板修改）
// 已按你的板子设置：SDA = GPIO20, SCL = GPIO21。
const uint8_t I2C_SDA_PIN = 20;
const uint8_t I2C_SCL_PIN = 21;

// 寄存器（来自你提供的寄存器图，已挑选常用寄存器）
const uint8_t REG_CHIPID = 0x00;   // CHIPID & DIAG
const uint8_t REG_STATUS = 0x01;   // 包含 UVLO/OVER_TEMP/OC_DETECT 等
const uint8_t REG_LRA_PERIOD_H = 0x05;
const uint8_t REG_LRA_PERIOD_L = 0x06;
const uint8_t REG_OPTIONS = 0x07;  // MODE / I2C_BCAST_EN / TRIG_PIN_FUNC ...
const uint8_t REG_CONTROL = 0x08;  // 控制循环 / AUTO_BRK / INPUT_SLOPE_CHECK 等
const uint8_t REG_GO = 0x0C;       // GO 位在此寄存器（bit0）
const uint8_t REG_RTP = 0x0E;      // RTP 输入（8-bit）
const uint8_t REG_WAIT1 = 0x0F;    // WAV/SEQUENCE 相关起始寄存器
const uint8_t REG_RATED_VOLT = 0x1F; // 额定电压
const uint8_t REG_RATED_VOLT_CLAMP = 0x24;

// 默认测试参数
const uint8_t TEST_RTP_VALUE = 0x40; // 中等幅度，按需调整（0-255）

// ---- I2C 读写 ----
static bool i2cWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(DRV_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool i2cWriteBuffer(uint8_t reg, const uint8_t *data, size_t len) {
  Wire.beginTransmission(DRV_I2C_ADDR);
  Wire.write(reg);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

static int i2cReadReg(uint8_t reg) {
  Wire.beginTransmission(DRV_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  Wire.requestFrom(DRV_I2C_ADDR, (uint8_t)1);
  if (Wire.available()) return Wire.read();
  return -1;
}

static bool scanI2CDevice() {
  Wire.beginTransmission(DRV_I2C_ADDR);
  return Wire.endTransmission() == 0;
}

// ---- 驱动器操作 ----
// 读取 CHIP ID 并打印
bool readChipId() {
  int v = i2cReadReg(REG_CHIPID);
  if (v < 0) return false;
  Serial.printf("CHIP ID / DIAG (0x%02X) = 0x%02X\n", REG_CHIPID, v);
  return true;
}

// 读取状态寄存器并显示（具体位请参照手册进行解释）
bool readStatus() {
  int v = i2cReadReg(REG_STATUS);
  if (v < 0) return false;
  Serial.printf("STATUS (0x%02X) = 0x%02X\n", REG_STATUS, v);
  // 根据你表中定义的位可以在此做位域解析，例如：
  // if (v & (1<<0)) Serial.println("OC_DETECT"); // 仅示例，请以手册为准
  return true;
}

// 写 RTP 值（RTP 为 8-bit：寄存器 0x0E）
bool writeRTP(uint8_t val) {
  if (!i2cWriteReg(REG_RTP, val)) return false;
  Serial.printf("写入 RTP (0x%02X) = 0x%02X\n", REG_RTP, val);
  return true;
}

// 触发 GO（在某些控制器中，写 GO=1 开始播放）
bool triggerGO() {
  if (!i2cWriteReg(REG_GO, 0x01)) return false;
  Serial.printf("写 GO (0x%02X) = 0x01 (触发播放)\n", REG_GO);
  return true;
}

// 清除 GO 位（写 0）
bool clearGO() {
  if (!i2cWriteReg(REG_GO, 0x00)) return false;
  Serial.printf("写 GO (0x%02X) = 0x00 (停止)\n", REG_GO);
  return true;
}

// 初始化驱动器：读取 CHIPID，设置一些常用寄存器（如 RATED_VOLT）
bool initDriver() {
  // 读取 CHIPID
  if (!readChipId()) return false;

  // 示例：设置额定电压为表中建议（0x3F 是示例值，请按表调整）
  i2cWriteReg(REG_RATED_VOLT, 0x3F);
  // 清除 GO
  i2cWriteReg(REG_GO, 0x00);

  // 额外初始化可写在这里（例如设置 MODE/CONTROL 寄存器）
  return true;
}

void printHelp() {
  Serial.println("命令: ");
  Serial.println(" i : 显示 CHIPID");
  Serial.println(" s : 显示 STATUS");
  Serial.println(" r : 写入 RTP 示例值并触发（写 RTP->trigger GO 可选）");
  Serial.println(" g : 只触发 GO");
  Serial.println(" c : 清除 GO (停止)");
  Serial.println(" v : 设置额定电压为示例值 (0x3F)");
  Serial.println(" h : 帮助");
}

void setup() {
  Serial.begin(115200);
  delay(10);

  if (FAULT_PIN >= 0) pinMode(FAULT_PIN, INPUT_PULLUP);

  // 显式使用指定的 SDA/SCL 引脚
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Serial.printf("I2C SDA pin: %d, SCL pin: %d\n", I2C_SDA_PIN, I2C_SCL_PIN);

  Serial.println("DRV2506L (粗略表) - I2C 测试程序");
  Serial.printf("使用 I2C 地址 0x%02X\n", DRV_I2C_ADDR);

  if (!scanI2CDevice()) {
    Serial.println("未检测到设备：请检查接线/地址");
    while (true) delay(1000);
  }

  Serial.println("设备在线，初始化...");
  if (!initDriver()) {
    Serial.println("初始化失败，请检查寄存器表与连接");
    while (true) delay(1000);
  }

  Serial.println("初始化完成。");
  printHelp();
}

void loop() {
  if (FAULT_PIN >= 0 && digitalRead(FAULT_PIN) == LOW) {
    Serial.println("硬件 FAULT 引脚触发 (LOW)，请检查驱动器/电源");
    while (true) delay(1000);
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'i') {
      readChipId();
    } else if (c == 's') {
      readStatus();
    } else if (c == 'r') {
      if (!writeRTP(TEST_RTP_VALUE)) Serial.println("写 RTP 失败");
      // 某些芯片写 RTP 后还需写 GO 才播放，若需要取消注释下一行
      // triggerGO();
    } else if (c == 'g') {
      triggerGO();
    } else if (c == 'c') {
      clearGO();
    } else if (c == 'v') {
      i2cWriteReg(REG_RATED_VOLT, 0x3F);
      Serial.println("已写额定电压示例值 0x3F");
    } else if (c == 'h') {
      printHelp();
    }
  }

  delay(20);
}
