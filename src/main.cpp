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
const unsigned long LONG_PRESS_TIME = 350;   // 判定为长按的等待阈值
const unsigned long DOUBLE_CLICK_TIME = 250; // 双击判定的最大间隔窗口
const unsigned long LONG_VIBE_LIMIT = 3000;  // 长按最大安全保护时间

// ---------- DRV2605 波形 ID (对照图表) ----------
#define WAVEFORM_SHORT_CLICK 1    // 强力点击 100% (用作点 '.')
#define WAVEFORM_LONG_BUZZ   47   // 蜂鸣 1 - 100% (用作划 '-')
#define WAVEFORM_HEART_1     1    // 心跳第一下
#define WAVEFORM_HEART_2     4    // 心跳第二下 (Sharp Click)

enum Mode { IDLE, PLAYING_SHORT, PLAYING_LONG, HEARTBEAT };
Mode currentMode = IDLE;

// 状态机核心变量
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

bool isPressed = false;
unsigned long pressStartTime = 0;
unsigned long releaseTime = 0;

int clickCount = 0;
bool waitingForClickTimeout = false;

// 持续性效果控制变量
unsigned long lastBuzzLoopTime = 0;
unsigned long longPressAutoStopTime = 0;
unsigned long heartbeatTimer = 0;
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
  writeReg(0x05, 0);        // 结束标记
  writeReg(0x0C, 0x01);     // GO!
}

void stopMotor() {
  writeReg(0x0C, 0x00);     // 立即刹车停止
}

void startHeartbeat() {
  currentMode = HEARTBEAT;
  heartbeatStep = 0;
  heartbeatTimer = millis();
  triggerWaveform(WAVEFORM_HEART_1);
  heartbeatStep = 1;
  Serial.println("[系统] 启动持续心跳模式");
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
  
  Serial.println("=== 优化版摩斯电键系统已就绪 ===");
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
      pressStartTime = now;
      waitingForClickTimeout = false; // 有新动作，中断上一次的单击结算窗口

      if (currentMode == HEARTBEAT) {
        // 如果当前在心跳，按下直接打断并退出心跳
        stopMotor();
        currentMode = IDLE;
        clickCount = 0;
        Serial.println("[系统] 心跳已打断");
      } else {
        // 实时手感：按下立刻先给一个短点击响应，如果是长按随后会自动转为长鸣
        currentMode = PLAYING_SHORT;
        triggerWaveform(WAVEFORM_SHORT_CLICK);
        clickCount++;
      }
    }

    // 【处于按住状态 - 实时判定是否转为长按】
    if (reading == LOW && isPressed && currentMode != PLAYING_LONG) {
      if (now - pressStartTime >= LONG_PRESS_TIME) {
        currentMode = PLAYING_LONG;
        triggerWaveform(WAVEFORM_LONG_BUZZ);
        lastBuzzLoopTime = now;
        longPressAutoStopTime = now + LONG_VIBE_LIMIT;
        clickCount = 0; // 长按不计入双击计数
        Serial.println("[摩斯码 - ] 长按启动");
      }
    }

    // 【松开瞬间 - 上升沿触发】
    if (reading == HIGH && isPressed) {
      isPressed = false;
      releaseTime = now;

      if (currentMode == PLAYING_LONG) {
        // 如果是长按松手，瞬间刹车
        stopMotor();
        currentMode = IDLE;
        Serial.println("[摩斯码 - ] 长按松开，刹车");
      } else if (currentMode == PLAYING_SHORT) {
        // 如果松手时还是短按状态，开启双击/单击等待窗口
        waitingForClickTimeout = true;
      }
    }
  }
  lastButtonState = reading;

  // 2. 异步结算 单击 vs 双击
  if (waitingForClickTimeout && (now - releaseTime > DOUBLE_CLICK_TIME)) {
    waitingForClickTimeout = false;
    if (clickCount == 1) {
      Serial.println("[摩斯码 . ] 单击确认");
      // 触发时已经在按下时响过了，这里只需结算状态
      currentMode = IDLE;
    } 
    else if (clickCount >= 2) {
      // 双击触发心跳
      stopMotor();
      startHeartbeat();
    }
    clickCount = 0;
  }

  // 3. 异步处理持续性硬件效果
  if (currentMode == PLAYING_LONG) {
    // 安全熔断保护
    if (now >= longPressAutoStopTime) {
      stopMotor();
      currentMode = IDLE;
      Serial.println("[安全切断] 长按满3秒强制停止");
    } 
    // 芯片内置波形 47 较短，若按住不放，每隔 180ms 自动重发波形维持震动
    else if (now - lastBuzzLoopTime >= 180) {
      triggerWaveform(WAVEFORM_LONG_BUZZ);
      lastBuzzLoopTime = now;
    }
  }

  // 异步处理心跳波形序列
  if (currentMode == HEARTBEAT) {
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