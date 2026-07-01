#include <Arduino.h>
#include <Wire.h>

// ---------- DRV2605 引脚定义 ----------
#define DRV2605_ADDR 0x5A
#define I2C_SDA 8
#define I2C_SCL 9

// ---------- 按钮引脚定义 ----------
#define BUTTON_PIN 0   // 使用 GPIO0，内部上拉

// ---------- 时间常量（单位：毫秒） ----------
const unsigned long DEBOUNCE_DELAY = 15;     // 硬件消抖时间
const unsigned long DOUBLE_CLICK_TIME = 250; // 双击判定的最大间隔窗口
const unsigned long LONG_VIBE_LIMIT = 5000;  // 长按最大安全保护时间为 5秒
const unsigned long HEARTBEAT_LIMIT = 20000; // 【新增强调】心跳自动停止阈值：20秒

// ---------- DRV2605 波形 ID ----------
#define WAVEFORM_LONG_BUZZ   47   // 蜂鸣 1 - 100% (RTP模式下此ID不再需要，但保留以防万一)
#define WAVEFORM_HEART_1     1    // 心跳第一下
#define WAVEFORM_HEART_2     4    // 心跳第二下

enum Mode { IDLE, PLAYING_LONG, HEARTBEAT };
Mode currentMode = IDLE;

// 状态机核心变量
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

bool isPressed = false;
unsigned long pressStartTime = 0;
unsigned long releaseTime = 0;

int clickCount = 0;

// 持续性效果控制变量
unsigned long longPressAutoStopTime = 0;

unsigned long heartbeatTimer = 0;
unsigned long heartbeatStartTime = 0; // 【新增】记录心跳总启动时间
int heartbeatStep = 0;

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(DRV2605_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void triggerWaveform(uint8_t effectId) {
  for (int i = 0x04; i <= 0x0B; i++) writeReg(i, 0); // 清空序列
  writeReg(0x04, effectId);
  writeReg(0x05, 0);        // 结束标志
  writeReg(0x0C, 0x01);     // GO!
}

void stopMotor() {
  writeReg(0x0C, 0x00);     // 立即刹车停止
}

void startHeartbeat() {
  currentMode = HEARTBEAT;
  heartbeatStep = 0;
  heartbeatTimer = millis();
  heartbeatStartTime = millis(); // 【新增】在启动时抓取当前的绝对时间
  triggerWaveform(WAVEFORM_HEART_1);
  heartbeatStep = 1;
  Serial.println("[系统] 启动持续心跳模式 (限时 20 秒)");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin(I2C_SDA, I2C_SCL);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 初始化 DRV2605 为波形库模式
  writeReg(0x01, 0x00);     // Internal Trigger Mode
  writeReg(0x1A, 0xB6);     // LRA 模式 (199Hz)
  writeReg(0x03, 0x06);     // 选择 LRA 官方效果库
  
  Serial.println("=== 已就绪 ===");
}

void loop() {
  int reading = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  // 1. 软件消抖
  if (reading != lastButtonState) {
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > DEBOUNCE_DELAY) {
    // 【按下瞬间 - 下降沿触发】
    if (reading == LOW && !isPressed) {
      isPressed = true;
      
      if (now - releaseTime <= DOUBLE_CLICK_TIME) {
        clickCount++;
      } else {
        clickCount = 1;
      }
      
      pressStartTime = now;

      if (currentMode == HEARTBEAT) {
        stopMotor();
        currentMode = IDLE;
        clickCount = 0;
        Serial.println("[系统] 心跳已由手动按键打断");
      } 
      else if (clickCount >= 2) {
        stopMotor();
        startHeartbeat();
        clickCount = 0; 
      } 
      else {
        currentMode = PLAYING_LONG;
        
        // 🔴【核心改动 1】长按进入 RTP 模式，实现绝对无间断平滑震动
        writeReg(0x01, 0x05);     // 切换到实时播放模式 (RTP Mode)
        writeReg(0x02, 0x7F);     // 写入震动强度的最大正值 (有符号模式下 0x7F 为全速)
        
        longPressAutoStopTime = now + LONG_VIBE_LIMIT; 
        Serial.println("[摩斯码] 按下 -> 开启 RTP 连续无缝震动");
      }
    }

    // 【松开瞬间 - 上升沿触发】
    if (reading == HIGH && isPressed) {
      isPressed = false;
      releaseTime = now;

      if (currentMode == PLAYING_LONG) {
        // 🔴【核心改动 2】松开时关闭 RTP 并恢复内部触发模式，解封状态机
        writeReg(0x02, 0x00);     // 驱动归零
        writeReg(0x01, 0x00);     // 切回内部触发模式 (Internal Trigger Mode)，给心跳用
        currentMode = IDLE;
        Serial.println("[摩斯码] 松开 -> RTP 停止并恢复就绪");
      }
    }
  }
  lastButtonState = reading;

  // 2. 异步处理持续性硬件效果
  if (currentMode == PLAYING_LONG) {
    if (now >= longPressAutoStopTime) {
      // 🔴【核心改动 3】安全切断时同样需要清理 RTP 并还原模式
      writeReg(0x02, 0x00);     // 驱动归零
      writeReg(0x01, 0x00);     // 切回内部触发模式
      currentMode = IDLE;
      Serial.println("[安全切断] 连续按住满 5 秒强制停止");
    } 
    // 🔴【核心改动 4】原本在这里每 180ms 重复 triggerWaveform 的间断刷新逻辑已被彻底移除
  }

  // 3. 异步处理心跳波形序列与【新增的20秒超时退出】
  if (currentMode == HEARTBEAT) {
    // 检查心跳总体播放时间是否达到了 20 秒
    if (now - heartbeatStartTime >= HEARTBEAT_LIMIT) {
      stopMotor();
      currentMode = IDLE;
      Serial.println("[安全切断] 心跳模式已满 20 秒，自动关闭");
    } 
    else {
      // 正常的心跳脉冲循环逻辑
      const unsigned long HEART_INTERVAL1 = 150; 
      const unsigned long HEART_INTERVAL2 = 700; 

      if (heartbeatStep == 1 && (now - heartbeatTimer >= HEART_INTERVAL1)) {
        triggerWaveform(WAVEFORM_HEART_2);
        heartbeatStep = 2;
        heartbeatTimer = now;
      } 
      else if (heartbeatStep == 2 && (now - heartbeatTimer >= HEART_INTERVAL2)) {
        triggerWaveform(WAVEFORM_HEART_1);
        heartbeatStep = 1;
        heartbeatTimer = now;
      }
    }
  }
}