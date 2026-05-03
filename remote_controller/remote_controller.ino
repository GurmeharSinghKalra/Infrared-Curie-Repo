#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Keypad.h>

// ==========================================
// CONFIGURATION
// ==========================================
const char* ssid = "Infrared Curie Setup"; // Default AP for the robot
const char* password = "";                 // Open network by default

const char* websocket_server = "192.168.4.1";
const uint16_t websocket_port = 80;
const char* websocket_path = "/ws";

// ==========================================
// PINOUT: KEYPAD (4x4)
// ==========================================
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {19, 18, 5, 17};
byte colPins[COLS] = {16, 4, 2, 15};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ==========================================
// PINOUT: JOYSTICKS
// ==========================================
#define L_JOY_X 32
#define L_JOY_Y 33
#define L_JOY_BTN 25

#define R_JOY_X 34
#define R_JOY_Y 35
#define R_JOY_BTN 26

// ==========================================
// STATE
// ==========================================
WebSocketsClient webSocket;
bool isConnected = false;

String lastDirection = "STOP";
unsigned long lastArmSendTime = 0;
int lastShoulder = 90;
int lastElbow = 45;

bool powerState = true;

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(L_JOY_BTN, INPUT_PULLUP);
  pinMode(R_JOY_BTN, INPUT_PULLUP);

  // Connect to WiFi
  Serial.println("Connecting to Curie WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: ");
  Serial.println(WiFi.localIP());

  // Setup WebSocket
  webSocket.begin(websocket_server, websocket_port, websocket_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(2000);
}

// ==========================================
// WEBSOCKET HANDLER
// ==========================================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected!");
      isConnected = false;
      break;
    case WStype_CONNECTED:
      Serial.printf("[WS] Connected to url: %s\n", payload);
      isConnected = true;
      break;
  }
}

// ==========================================
// HELPER: SEND JSON COMMAND
// ==========================================
void sendCommand(JsonDocument& doc) {
  if (!isConnected) return;
  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
  Serial.println("Sent: " + output);
}

// ==========================================
// LOGIC: READ KEYPAD
// ==========================================
void processKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  JsonDocument doc;
  
  switch(key) {
    // --- EXPRESSIONS ---
    case '1': doc["cmd"] = "anim"; doc["animation"] = "happy"; break;
    case '2': doc["cmd"] = "anim"; doc["animation"] = "neutral"; break;
    case '3': doc["cmd"] = "anim"; doc["animation"] = "sad"; break;
    case '4': doc["cmd"] = "anim"; doc["animation"] = "wink"; break;
    case '5': doc["cmd"] = "anim"; doc["animation"] = "love"; break;
    case '6': doc["cmd"] = "anim"; doc["animation"] = "angry"; break;
    case '7': doc["cmd"] = "anim"; doc["animation"] = "sleep"; break;
    case '8': doc["cmd"] = "anim"; doc["animation"] = "scan"; break;
    
    // --- MODES & POWER ---
    case '9': doc["cmd"] = "demo"; break;
    case '0': 
      powerState = !powerState;
      doc["cmd"] = "power"; 
      doc["state"] = powerState ? "on" : "off"; 
      break;
      
    // --- SPEED PROFILES ---
    case 'A': doc["cmd"] = "profile"; doc["profile"] = "smooth"; break;
    case 'B': doc["cmd"] = "profile"; doc["profile"] = "normal"; break;
    case 'C': doc["cmd"] = "profile"; doc["profile"] = "aggressive"; break;
    
    // --- EMERGENCY STOP ---
    case 'D': 
    case '*':
    case '#':
      doc["cmd"] = "move"; doc["dir"] = "STOP"; 
      break;
  }
  
  if (!doc.isNull()) {
    sendCommand(doc);
  }
}

// ==========================================
// LOGIC: READ LEFT JOYSTICK (MOVEMENT)
// ==========================================
void processMovement() {
  int x = analogRead(L_JOY_X);
  int y = analogRead(L_JOY_Y);
  
  // Deadzone around center (center is ~2000-2100 on ESP32)
  String dir = "STOP";
  
  if (y < 1000) dir = "FWD";
  else if (y > 3000) dir = "BWD";
  else if (x < 1000) dir = "LEFT";
  else if (x > 3000) dir = "RIGHT";

  // Diagonals (optional, add if needed)
  if (y < 1000 && x < 1000) dir = "FWD_LEFT";
  if (y < 1000 && x > 3000) dir = "FWD_RIGHT";
  if (y > 3000 && x < 1000) dir = "BWD_LEFT";
  if (y > 3000 && x > 3000) dir = "BWD_RIGHT";

  if (dir != lastDirection) {
    JsonDocument doc;
    if (dir == "STOP") {
      doc["cmd"] = "stop"; // You can use "stop" or {"cmd":"move","dir":"STOP"}
    } else {
      doc["cmd"] = "move";
      doc["dir"] = dir;
    }
    sendCommand(doc);
    lastDirection = dir;
  }
}

// ==========================================
// LOGIC: READ RIGHT JOYSTICK (ARMS)
// ==========================================
void processArms() {
  // If button pressed, reset to default
  if (digitalRead(R_JOY_BTN) == LOW) {
    JsonDocument doc;
    doc["cmd"] = "arms";
    doc["ls"] = 90; doc["rs"] = 90;
    doc["le"] = 45; doc["re"] = 45;
    sendCommand(doc);
    delay(500); // Debounce
    return;
  }

  // Only send arm updates every 100ms to avoid flooding WebSocket
  if (millis() - lastArmSendTime < 100) return;

  int x = analogRead(R_JOY_X);
  int y = analogRead(R_JOY_Y);
  
  // Map analog reading (0-4095) to servo angles
  // y controls shoulders (0 to 180)
  // x controls elbows (0 to 180)
  int shoulder = map(y, 0, 4095, 0, 180);
  int elbow = map(x, 0, 4095, 0, 180);

  // Deadzone filter (only send if changed by > 5 degrees)
  if (abs(shoulder - lastShoulder) > 5 || abs(elbow - lastElbow) > 5) {
    JsonDocument doc;
    doc["cmd"] = "arms";
    // For simplicity, mirror the movement to both arms
    doc["ls"] = shoulder;
    doc["rs"] = 180 - shoulder; // Invert right shoulder to match visually
    doc["le"] = elbow;
    doc["re"] = 180 - elbow;    // Invert right elbow
    
    sendCommand(doc);
    
    lastShoulder = shoulder;
    lastElbow = elbow;
    lastArmSendTime = millis();
  }
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  webSocket.loop();
  
  if (isConnected) {
    processKeypad();
    processMovement();
    processArms();
  }
}
