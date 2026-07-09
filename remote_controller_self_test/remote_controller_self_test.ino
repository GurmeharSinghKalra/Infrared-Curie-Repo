#include <Arduino.h>
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

static BLEUUID serviceUUID("12345678-1234-5678-1234-56789abcdef0");
static BLEUUID charUUID("12345678-1234-5678-1234-56789abcdef1");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

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
  if (!connected || pRemoteCharacteristic == nullptr) {
    return;
  }

  uint8_t packet[8] = {0};
  packet[0] = CURIE_BLE_MAGIC;
  packet[1] = throttle;
  packet[2] = steering;
  packet[3] = buttons;
  packet[4] = expression_id;
  packet[5] = speed_mode;
  packet[6] = s_sequence++;
  packet[7] = checksum_xor(packet);

  pRemoteCharacteristic->writeValue(packet, sizeof(packet), false);
  Serial.print("step=");
  Serial.print(s_step);
  Serial.println(" sent");
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }
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
      Serial.print("Failed to find service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
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
  Serial.println("Curie BLE controller self-test boot");
  
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);

  s_last_step_ms = millis();
}

void loop() {
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("Ready to send.");
    } else {
      Serial.println("Failed to connect.");
    }
    doConnect = false;
  }

  if (doScan) {
    BLEDevice::getScan()->start(5, false);
    doScan = false;
  }

  if (connected) {
    unsigned long now = millis();
    if (now - s_last_step_ms >= 2500) {
      run_test_step();
      s_last_step_ms = now;
    }
  } else {
    delay(250);
  }
}
