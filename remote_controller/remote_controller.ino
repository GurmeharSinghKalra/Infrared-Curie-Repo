#include <Arduino.h>
#include <Keypad.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

static const uint8_t CURIE_BLE_MAGIC = 0xC4;
static const uint8_t CURIE_CTRL_BTN_ESTOP = 1 << 0;
static const uint8_t CURIE_CTRL_BTN_HOME = 1 << 1;
static const uint8_t CURIE_CTRL_BTN_ARM_UP = 1 << 2;
static const uint8_t CURIE_CTRL_BTN_ARM_DOWN = 1 << 3;
static const uint8_t CURIE_CTRL_BTN_CLEAR_ESTOP = 1 << 4;
static const uint8_t CURIE_CTRL_BTN_BLINK = 1 << 5;

static const uint32_t SEND_INTERVAL_MS = 40;
static const uint32_t SHOULDER_HOLD_REPEAT_MS = 60;
static const uint32_t HOME_HOLD_REPEAT_MS = 350;
static const uint32_t CLEAR_HOLD_MS = 1200;

static BLEUUID serviceUUID("12345678-1234-5678-1234-56789abcdef0");
static BLEUUID charUUID("12345678-1234-5678-1234-56789abcdef1");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {19, 18, 5, 17};
byte colPins[COLS] = {16, 4, 2, 15};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

static const int PIN_JOY_X = 32;
static const int PIN_JOY_Y = 33;
static const int PIN_JOY_SW = 25;

static const int PIN_BTN_UP = 26;
static const int PIN_BTN_RIGHT = 27;
static const int PIN_BTN_DOWN = 21;
static const int PIN_BTN_LEFT = 22;

static uint8_t s_sequence = 0;
static uint8_t s_speed_mode = 1;
static uint8_t s_pending_expression = 0;
static uint8_t s_pending_button_latch = 0;
static uint8_t s_expression_page = 0;
static unsigned long s_last_send_ms = 0;

static bool s_joy_button_down = false;
static unsigned long s_joy_button_press_ms = 0;
static bool s_joy_long_handled = false;

static unsigned long s_last_arm_repeat_ms = 0;
static unsigned long s_last_home_repeat_ms = 0;

static uint8_t checksum_xor(const uint8_t *data) {
  uint8_t out = 0;
  for (int i = 0; i < 7; i++) {
    out ^= data[i];
  }
  return out;
}

static uint8_t clamp_to_byte(int value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return (uint8_t)value;
}

static uint8_t analog_to_axis_byte(int raw, bool invert) {
  const int center = 2048;
  const int deadzone = 250;
  int adjusted = raw;

  if (abs(raw - center) <= deadzone) {
    return 128;
  }

  adjusted = map(raw, 0, 4095, 0, 255);
  if (invert) {
    adjusted = 255 - adjusted;
  }
  return clamp_to_byte(adjusted);
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {}
  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnected");
    doScan = true;
  }
};

bool connectToServer() {
    Serial.print("Connecting to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient* pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    if (!pClient->connect(myDevice)) return false;
    Serial.println("Connected to server");

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      pClient->disconnect();
      return false;
    }

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      pClient->disconnect();
      return false;
    }

    connected = true;
    return true;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getName() == "Curie-Robot") {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;
    }
  }
};

static void queue_expression(uint8_t id) {
  s_pending_expression = id;
}

static void queue_expression_from_key(char key) {
  if (key == 'D') {
    s_expression_page ^= 1;
    return;
  }
  if (key == '*') {
    s_pending_button_latch |= CURIE_CTRL_BTN_BLINK;
    return;
  }
  if (key == '#') {
    queue_expression((uint8_t)random(1, 13));
    return;
  }
  if (key == '0') {
    queue_expression(13);
    return;
  }

  if (s_expression_page == 0) {
    switch (key) {
      case '1': queue_expression(1); break;
      case '2': queue_expression(2); break;
      case '3': queue_expression(3); break;
      case '4': queue_expression(4); break;
      case '5': queue_expression(5); break;
      case '6': queue_expression(6); break;
      case '7': queue_expression(7); break;
      case '8': queue_expression(8); break;
      case '9': queue_expression(9); break;
      case 'A': queue_expression(10); break;
      case 'B': queue_expression(11); break;
      case 'C': queue_expression(12); break;
    }
  } else {
    switch (key) {
      case '1': queue_expression(16); break;
      case '2': queue_expression(17); break;
      case '3': queue_expression(15); break;
      case '4': queue_expression(14); break;
      case '5': queue_expression(8); break;
      case '6': queue_expression(10); break;
      case '7': queue_expression(11); break;
      case '8': queue_expression(12); break;
      case '9': queue_expression(4); break;
      case 'A': queue_expression(5); break;
      case 'B': queue_expression(6); break;
      case 'C': queue_expression(7); break;
    }
  }
}

static void process_keypad(void) {
  char key = keypad.getKey();
  if (key) {
    queue_expression_from_key(key);
  }
}

static void process_joystick_button(void) {
  bool pressed = digitalRead(PIN_JOY_SW) == LOW;
  unsigned long now = millis();

  if (pressed && !s_joy_button_down) {
    s_joy_button_down = true;
    s_joy_button_press_ms = now;
    s_joy_long_handled = false;
  } else if (!pressed && s_joy_button_down) {
    if (!s_joy_long_handled) {
      s_speed_mode = (s_speed_mode + 1) % 3;
    }
    s_joy_button_down = false;
  }

  if (pressed && !s_joy_long_handled && (now - s_joy_button_press_ms) >= CLEAR_HOLD_MS) {
    s_pending_button_latch |= CURIE_CTRL_BTN_CLEAR_ESTOP;
    s_joy_long_handled = true;
  }
}

static uint8_t collect_buttons(void) {
  unsigned long now = millis();
  uint8_t buttons = s_pending_button_latch;
  s_pending_button_latch = 0;

  bool arm_up = digitalRead(PIN_BTN_UP) == LOW;
  bool arm_down = digitalRead(PIN_BTN_DOWN) == LOW;
  bool home = digitalRead(PIN_BTN_LEFT) == LOW;
  bool estop = digitalRead(PIN_BTN_RIGHT) == LOW;

  if (estop) buttons |= CURIE_CTRL_BTN_ESTOP;

  if (home && (now - s_last_home_repeat_ms >= HOME_HOLD_REPEAT_MS)) {
    buttons |= CURIE_CTRL_BTN_HOME;
    s_last_home_repeat_ms = now;
  }

  if ((arm_up || arm_down) && (now - s_last_arm_repeat_ms >= SHOULDER_HOLD_REPEAT_MS)) {
    if (arm_up) buttons |= CURIE_CTRL_BTN_ARM_UP;
    if (arm_down) buttons |= CURIE_CTRL_BTN_ARM_DOWN;
    s_last_arm_repeat_ms = now;
  }

  return buttons;
}

static void send_packet(void) {
  if (!connected || pRemoteCharacteristic == nullptr) {
    return;
  }

  uint8_t packet[8] = {0};
  packet[0] = CURIE_BLE_MAGIC;
  packet[1] = analog_to_axis_byte(analogRead(PIN_JOY_Y), true);
  packet[2] = analog_to_axis_byte(analogRead(PIN_JOY_X), false);
  packet[3] = collect_buttons();
  packet[4] = s_pending_expression;
  packet[5] = s_speed_mode;
  packet[6] = s_sequence++;
  packet[7] = checksum_xor(packet);

  pRemoteCharacteristic->writeValue(packet, sizeof(packet), false);
  s_pending_expression = 0;
}

void setup() {
  Serial.begin(115200);
  randomSeed((uint32_t)esp_random());

  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);

  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);

  Serial.println("Curie BLE Controller ready");
}

void loop() {
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("Ready to send.");
    }
    doConnect = false;
  }

  if (doScan) {
    BLEDevice::getScan()->start(5, false);
    doScan = false;
  }

  process_keypad();
  process_joystick_button();

  unsigned long now = millis();
  if (now - s_last_send_ms >= SEND_INTERVAL_MS) {
    send_packet();
    s_last_send_ms = now;
  }
}
