#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

static const uint8_t CURIE_ESPNOW_MAGIC = 0xC4;
static const uint8_t CURIE_CTRL_BTN_ESTOP = 1 << 0;
static const uint8_t CURIE_CTRL_BTN_HOME = 1 << 1;
static const uint8_t CURIE_CTRL_BTN_ARM_UP = 1 << 2;
static const uint8_t CURIE_CTRL_BTN_ARM_DOWN = 1 << 3;
static const uint8_t CURIE_CTRL_BTN_CLEAR_ESTOP = 1 << 4;
static const uint8_t CURIE_CTRL_BTN_BLINK = 1 << 5;

static const uint8_t ROBOT_AP_CHANNEL = 1;
static const uint8_t BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool s_espnow_ready = false;
static uint8_t s_sequence = 0;
static unsigned long s_last_step_ms = 0;
static int s_step = 0;

static uint8_t checksum_xor(const uint8_t *data) {
  uint8_t out = 0;
  for (int i = 0; i < 7; i++) {
    out ^= data[i];
  }
  return out;
}

static void send_packet(uint8_t throttle, uint8_t steering, uint8_t buttons, uint8_t expression_id, uint8_t speed_mode) {
  if (!s_espnow_ready) {
    return;
  }

  uint8_t packet[8] = {0};
  packet[0] = CURIE_ESPNOW_MAGIC;
  packet[1] = throttle;
  packet[2] = steering;
  packet[3] = buttons;
  packet[4] = expression_id;
  packet[5] = speed_mode;
  packet[6] = s_sequence++;
  packet[7] = checksum_xor(packet);

  esp_err_t err = esp_now_send(BROADCAST_ADDR, packet, sizeof(packet));
  Serial.print("step=");
  Serial.print(s_step);
  Serial.print(" send=");
  Serial.println((int)err);
}

static void init_espnow(void) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);

  esp_wifi_set_channel(ROBOT_AP_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

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
  Serial.println("Controller self-test ready on channel 1");
}

static void run_test_step(void) {
  switch (s_step) {
    case 0:
      Serial.println("neutral");
      send_packet(128, 128, 0, 13, 1);
      break;
    case 1:
      Serial.println("happy");
      send_packet(128, 128, 0, 1, 1);
      break;
    case 2:
      Serial.println("blink");
      send_packet(128, 128, CURIE_CTRL_BTN_BLINK, 0, 1);
      break;
    case 3:
      Serial.println("shoulders up");
      send_packet(128, 128, CURIE_CTRL_BTN_ARM_UP, 0, 1);
      break;
    case 4:
      Serial.println("shoulders down");
      send_packet(128, 128, CURIE_CTRL_BTN_ARM_DOWN, 0, 1);
      break;
    case 5:
      Serial.println("home");
      send_packet(128, 128, CURIE_CTRL_BTN_HOME, 0, 1);
      break;
    case 6:
      Serial.println("drive forward");
      send_packet(32, 128, 0, 0, 1);
      break;
    case 7:
      Serial.println("drive stop");
      send_packet(128, 128, 0, 0, 1);
      break;
    case 8:
      Serial.println("estop");
      send_packet(128, 128, CURIE_CTRL_BTN_ESTOP, 0, 1);
      break;
    case 9:
      Serial.println("clear estop");
      send_packet(128, 128, CURIE_CTRL_BTN_CLEAR_ESTOP, 0, 1);
      break;
    case 10:
      Serial.println("excited");
      send_packet(128, 128, 0, 12, 2);
      break;
    case 11:
      Serial.println("surprised");
      send_packet(128, 128, 0, 11, 2);
      break;
    case 12:
      Serial.println("thoughtful");
      send_packet(128, 128, 0, 8, 1);
      break;
      default:
      s_step = -1;
      break;
  }

  s_step++;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Curie controller self-test boot");
  init_espnow();
  s_last_step_ms = millis();
}

void loop() {
  if (!s_espnow_ready) {
    delay(250);
    return;
  }

  unsigned long now = millis();
  if (now - s_last_step_ms >= 2500) {
    run_test_step();
    s_last_step_ms = now;
  }
}
