#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <Keypad.h>

// ==========================================
// CONTROLLER -> ROBOT ESP-NOW PACKET
// ==========================================
static const uint8_t CURIE_ESPNOW_MAGIC = 0xC4;
static const uint8_t CURIE_CTRL_BTN_ESTOP = 1 << 0;
static const uint8_t CURIE_CTRL_BTN_HOME = 1 << 1;
static const uint8_t CURIE_CTRL_BTN_ARM_UP = 1 << 2;
static const uint8_t CURIE_CTRL_BTN_ARM_DOWN = 1 << 3;
static const uint8_t CURIE_CTRL_BTN_CLEAR_ESTOP = 1 << 4;
static const uint8_t CURIE_CTRL_BTN_BLINK = 1 << 5;

static const uint8_t ROBOT_AP_CHANNEL = 1;
static const uint8_t BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint32_t SEND_INTERVAL_MS = 40;
static const uint32_t SHOULDER_HOLD_REPEAT_MS = 60;
static const uint32_t HOME_HOLD_REPEAT_MS = 350;
static const uint32_t CLEAR_HOLD_MS = 1200;

// ==========================================
// PINOUT
// ==========================================
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

// ==========================================
// STATE
// ==========================================
static bool s_espnow_ready = false;
static uint8_t s_sequence = 0;
static uint8_t s_speed_mode = 1;  // 0=40%, 1=70%, 2=100%
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

static void on_data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("ESP-NOW send failed");
  }
}

static void init_espnow(void) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);

  if (esp_wifi_set_channel(ROBOT_AP_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println("Failed to set Wi-Fi channel for ESP-NOW");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(on_data_sent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST_ADDR, sizeof(BROADCAST_ADDR));
  peer.channel = ROBOT_AP_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  esp_now_del_peer(BROADCAST_ADDR);
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW broadcast peer");
    return;
  }

  s_espnow_ready = true;
  Serial.println("ESP-NOW controller ready on channel 1");
}

static const char* expression_name_from_id(uint8_t id) {
  switch (id) {
    case 1:  return "happy";
    case 2:  return "sad";
    case 3:  return "angry";
    case 4:  return "fear";
    case 5:  return "disgust";
    case 6:  return "confused";
    case 7:  return "contempt";
    case 8:  return "thoughtful";
    case 9:  return "shy";
    case 10: return "funny";
    case 11: return "surprised";
    case 12: return "excited";
    case 13: return "neutral";
    case 14: return "wink";
    case 15: return "love";
    case 16: return "sleep";
    case 17: return "scan";
    default: return "none";
  }
}

static void queue_expression(uint8_t id) {
  s_pending_expression = id;
  Serial.print("Expression -> ");
  Serial.println(expression_name_from_id(id));
}

static void queue_expression_from_key(char key) {
  if (key == 'D') {
    s_expression_page ^= 1;
    Serial.print("Expression page -> ");
    Serial.println(s_expression_page == 0 ? "core" : "special");
    return;
  }

  if (key == '*') {
    s_pending_button_latch |= CURIE_CTRL_BTN_BLINK;
    Serial.println("Blink queued");
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
      default: break;
    }
    return;
  }

  switch (key) {
    case '1': queue_expression(16); break;  // sleep
    case '2': queue_expression(17); break;  // scan
    case '3': queue_expression(15); break;  // love
    case '4': queue_expression(14); break;  // wink
    case '5': queue_expression(8); break;   // thoughtful
    case '6': queue_expression(10); break;  // funny
    case '7': queue_expression(11); break;  // surprised
    case '8': queue_expression(12); break;  // excited
    case '9': queue_expression(4); break;   // fear
    case 'A': queue_expression(5); break;   // disgust
    case 'B': queue_expression(6); break;   // confused
    case 'C': queue_expression(7); break;   // contempt
    default: break;
  }
}

static void process_keypad(void) {
  char key = keypad.getKey();
  if (!key) return;

  queue_expression_from_key(key);
  Serial.print("Key pressed: ");
  Serial.println(key);
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
      Serial.print("Speed mode -> ");
      Serial.println(s_speed_mode);
    }
    s_joy_button_down = false;
  }

  if (pressed && !s_joy_long_handled && (now - s_joy_button_press_ms) >= CLEAR_HOLD_MS) {
    s_pending_button_latch |= CURIE_CTRL_BTN_CLEAR_ESTOP;
    s_joy_long_handled = true;
    Serial.println("Queued clear estop");
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

  if (estop) {
    buttons |= CURIE_CTRL_BTN_ESTOP;
  }

  if (home && (now - s_last_home_repeat_ms >= HOME_HOLD_REPEAT_MS)) {
    buttons |= CURIE_CTRL_BTN_HOME;
    s_last_home_repeat_ms = now;
  }

  if ((arm_up || arm_down) && (now - s_last_arm_repeat_ms >= SHOULDER_HOLD_REPEAT_MS)) {
    if (arm_up) {
      buttons |= CURIE_CTRL_BTN_ARM_UP;
    }
    if (arm_down) {
      buttons |= CURIE_CTRL_BTN_ARM_DOWN;
    }
    s_last_arm_repeat_ms = now;
  }

  return buttons;
}

static void send_packet(void) {
  if (!s_espnow_ready) {
    return;
  }

  uint8_t packet[8] = {0};
  packet[0] = CURIE_ESPNOW_MAGIC;
  packet[1] = analog_to_axis_byte(analogRead(PIN_JOY_Y), true);
  packet[2] = analog_to_axis_byte(analogRead(PIN_JOY_X), false);
  packet[3] = collect_buttons();
  packet[4] = s_pending_expression;
  packet[5] = s_speed_mode;
  packet[6] = s_sequence++;
  packet[7] = checksum_xor(packet);

  esp_err_t err = esp_now_send(BROADCAST_ADDR, packet, sizeof(packet));
  if (err != ESP_OK) {
    Serial.print("esp_now_send error: ");
    Serial.println((int)err);
  }

  s_pending_expression = 0;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Curie controller boot");
  randomSeed((uint32_t)esp_random());

  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);

  init_espnow();
}

void loop() {
  process_keypad();
  process_joystick_button();

  unsigned long now = millis();
  if (now - s_last_send_ms >= SEND_INTERVAL_MS) {
    send_packet();
    s_last_send_ms = now;
  }
}
