#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// BLEシリアル通信の標準的なUUID（Nordic UART Service）
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // Web → ESP32
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // ESP32 → Web

#define DEVICE_NAME "ESP32-Splinkler" // スマホに表示される名前（index.htmlと合わせる）

#define LED_PIN 8 // 内蔵LEDのGPIO番号
// ESP32-C3 Super Miniなど、GPIO8のLEDはLOWで点灯する基板が多い。
// 待機中にLEDが点きっぱなしになる場合は HIGH と LOW を入れ替える。
#define LED_ON_LEVEL  LOW
#define LED_OFF_LEVEL HIGH

#define BLINK_ON_MS  1000 // 点灯している時間
#define BLINK_OFF_MS 1000 // 消灯している時間
#define MAX_BLINK_COUNT 100

// 受信コマンドの最大長（終端文字を含む）
#define COMMAND_MAX_LEN 32

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;

// BLEのコールバックは別タスクで動くため、受信したコマンドはキューでloop()へ渡す
QueueHandle_t commandQueue;
String serialLine; // シリアルモニタから入力中のコマンド

// 点滅の状態（loop()からのみ操作する）
int blinkTotal = 0;  // 今回の点滅回数（0なら停止中）
int blinkCount = 0;  // 点灯した回数
bool ledOn = false;
unsigned long lastToggleMs = 0;

// 接続状態のコールバック
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("スマホと接続しました！");
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("スマホとの接続が切れました。");
      pServer->startAdvertising(); // 切断後に再検索可能にする
    }
};

// Webからデータを受信したときのコールバック
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {
        char command[COMMAND_MAX_LEN] = {0};
        strncpy(command, rxValue.c_str(), COMMAND_MAX_LEN - 1);
        xQueueSend(commandQueue, command, 0);
      }
    }
};

// Webへ状態を通知する（BLEの既定MTUに収まるよう20バイト以内の英数字にする）
void notifyWeb(const String &message) {
  Serial.print("Webへ送信: ");
  Serial.println(message);
  if (deviceConnected) {
    pTxCharacteristic->setValue(message.c_str());
    pTxCharacteristic->notify();
  }
}

void setLed(bool on) {
  digitalWrite(LED_PIN, on ? LED_ON_LEVEL : LED_OFF_LEVEL);
  ledOn = on;
}

void startBlink(int count) {
  blinkTotal = count;
  blinkCount = 1;
  setLed(true);
  lastToggleMs = millis();
  notifyWeb("ON 1/" + String(count));
}

void stopBlink() {
  blinkTotal = 0;
  setLed(false);
  notifyWeb("STOPPED");
}

// millis()で経過時間を見て点灯・消灯を切り替える（delayで止めずにBLE受信を続ける）
void updateBlink() {
  if (blinkTotal == 0) return;

  unsigned long now = millis();
  if (ledOn) {
    if (now - lastToggleMs < BLINK_ON_MS) return;
    setLed(false);
    lastToggleMs = now;
    if (blinkCount >= blinkTotal) {
      notifyWeb("DONE " + String(blinkTotal));
      blinkTotal = 0;
    }
  } else {
    if (now - lastToggleMs < BLINK_OFF_MS) return;
    blinkCount++;
    setLed(true);
    lastToggleMs = now;
    notifyWeb("ON " + String(blinkCount) + "/" + String(blinkTotal));
  }
}

// コマンドの形式
//   BLINK <回数>  指定回数だけ点滅する（点滅中に受けた場合は最初からやり直す）
//   STOP          点滅を止める
void handleCommand(String command) {
  command.trim();
  command.toUpperCase();
  Serial.print("受信: ");
  Serial.println(command);

  if (command == "STOP") {
    stopBlink();
  } else if (command.startsWith("BLINK ")) {
    int count = command.substring(6).toInt(); // 数値でなければ0になる
    if (count < 1 || count > MAX_BLINK_COUNT) {
      notifyWeb("ERR RANGE 1-" + String(MAX_BLINK_COUNT));
      return;
    }
    startBlink(count);
  } else {
    notifyWeb("ERR UNKNOWN");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  setLed(false);
  delay(1000); // ESP32-C3のUSBシリアル認識待ち
  Serial.println("BLE起動中...");

  commandQueue = xQueueCreate(8, COMMAND_MAX_LEN);

  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // 送信用キャラクタリスティックの作成 (Notify)
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // 受信用キャラクタリスティックの作成 (Write)
  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(
                       CHARACTERISTIC_UUID_RX,
                       BLECharacteristic::PROPERTY_WRITE
                     );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("準備完了！スマホからの接続を待っています。");
}

void loop() {
  // Webから受信したコマンドを処理
  char command[COMMAND_MAX_LEN];
  while (xQueueReceive(commandQueue, command, 0) == pdTRUE) {
    handleCommand(String(command));
  }

  // シリアルモニタからも同じコマンドで動作確認できるようにする
  // （モニタは1文字ずつ送ってくるため、改行が来るまで貯めてから処理する）
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLine.length() > 0) handleCommand(serialLine);
      serialLine = "";
    } else {
      serialLine += c;
    }
  }

  updateBlink();
  delay(10);
}
