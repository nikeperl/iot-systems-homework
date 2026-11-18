#include "secrets.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <vector>

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* slave_ip = "192.168.68.129";

const int RED_PIN    = 14; // D5
const int YELLOW_PIN = 12; // D6
const int GREEN_PIN  = 13; // D7
const int BUTTON_PIN = 0;

struct TrafficLight { bool r, y, g; };
struct State { std::vector<TrafficLight> directions; int duration; };
typedef std::vector<State> Mode;

std::vector<Mode> modes;
int currentMode = 0;
int currentState = 0;
unsigned long lastStepTime = 0;

bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void setupTrafficLogic() {
  Mode regulated;

  regulated.push_back({{{false, false, true}, {true,  false, false}}, 6});
  regulated.push_back({{{false, false, true}, {true,  true, false}}, 1});
  regulated.push_back({{{false, false, false}, {false, true, false}}, 1});
  regulated.push_back({{{false, false, true}, {true,  true, false}}, 1});
  regulated.push_back({{{false, false, false}, {false, true, false}}, 1});
  regulated.push_back({{{false, false, true}, {true,  true, false}}, 1});
  regulated.push_back({{{false, false, false}, {false, true, false}}, 1});
  regulated.push_back({{{false, true,  false}, {false, true,  false}}, 3});
  regulated.push_back({{{true,  false, false}, {false, false, true}}, 6});
  regulated.push_back({{{true,  true, false}, {false, false, true}}, 1});
  regulated.push_back({{{false, true, false}, {false, false, false}}, 1});
  regulated.push_back({{{true,  true, false}, {false, false, true}}, 1});
  regulated.push_back({{{false, true, false}, {false, false, false}}, 1});
  regulated.push_back({{{true,  true, false}, {false, false, true}}, 1});
  regulated.push_back({{{false, true, false}, {false, false, false}}, 1});
  regulated.push_back({{{false, true, false}, {false, true, false}}, 3});

  Mode unregulated;
  unregulated.push_back({{{false, true, false}, {false, true, false}}, 1});
  unregulated.push_back({{{false, false, false}, {false, false, false}}, 1});

  modes.push_back(regulated);
  modes.push_back(unregulated);
}

void sendStateHTTP(TrafficLight slave) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + slave_ip + "/state";

  http.begin(client, url);
  http.addHeader("Content-Type", "text/plain");

  String msg = String(slave.r) + String(slave.y) + String(slave.g);

  http.POST(msg);
  http.end();
}

void applyCurrentState() {
  Mode& traffic_modes = modes[currentMode];

  TrafficLight master = traffic_modes[currentState].directions[0];
  TrafficLight slave  = traffic_modes[currentState].directions[1];

  digitalWrite(RED_PIN, master.r);
  digitalWrite(YELLOW_PIN, master.y);
  digitalWrite(GREEN_PIN, master.g);

  // 👉 ВМЕСТО MQTT publish
  sendStateHTTP(slave);
}

void switchMode() {
  currentMode = (currentMode + 1) % modes.size();
  currentState = 0;
  lastStepTime = millis();
  applyCurrentState();
}

void checkButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading)
    lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW) {
      switchMode();
      lastButtonReading = HIGH;
      while (digitalRead(BUTTON_PIN) == LOW);
    }
  }

  lastButtonReading = reading;
}

void setup() {
  Serial.begin(115200);

  pinMode(RED_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  setupTrafficLogic();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  applyCurrentState();
}

void loop() {
  checkButton();

  if (millis() - lastStepTime >= (unsigned long)modes[currentMode][currentState].duration * 1000) {
    currentState = (currentState + 1) % modes[currentMode].size();
    lastStepTime = millis();
    applyCurrentState();
  }
}
