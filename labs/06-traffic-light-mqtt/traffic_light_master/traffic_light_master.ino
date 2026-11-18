#include "secrets.h"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <vector>

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* mqtt_server = "192.168.68.73";

WiFiClient espClient;
PubSubClient client(espClient);

// Пины светофора
const int RED_PIN    = 14; // D5
const int YELLOW_PIN = 12; // D6
const int GREEN_PIN  = 13; // D7

// Кнопка FLASH подключена к GPIO 0 (D3)
const int BUTTON_PIN = 0;

struct TrafficLight { bool r, y, g; };
struct State { std::vector<TrafficLight> directions; int duration; };
typedef std::vector<State> Mode;

std::vector<Mode> modes;
int currentMode = 0;
int currentState = 0;
unsigned long lastStepTime = 0;
unsigned long lastMqttAttempt = 0; // Для неблокирующего реконнекта

// Антидребезг
bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void setupTrafficLogic() {
  Mode regulated;
  // (Ваша таблица логики)
  regulated.push_back({{{false, false, true}, {true,  false, false}}, 6});
  regulated.push_back({{{false, false, false}, {true,  false, false}}, 1});
  regulated.push_back({{{false, false, true}, {true,  false, false}}, 1});
  regulated.push_back({{{false, false, false}, {true,  false, false}}, 1});
  regulated.push_back({{{false, false, true}, {true,  false, false}}, 1});
  regulated.push_back({{{false, false, false}, {true,  false, false}}, 1});
  regulated.push_back({{{false, false, true}, {true,  false, false}}, 1});
  regulated.push_back({{{false, true,  false}, {true,  true,  false}}, 3});
  regulated.push_back({{{true,  false, false}, {false, false, true}}, 6});
  regulated.push_back({{{true,  false, false}, {false, false, false}}, 1});
  regulated.push_back({{{true,  false, false}, {false, false, true}}, 1});
  regulated.push_back({{{true,  false, false}, {false, false, false}}, 1});
  regulated.push_back({{{true,  false, false}, {false, false, true}}, 1});
  regulated.push_back({{{true,  false, false}, {false, false, false}}, 1});
  regulated.push_back({{{true,  false, false}, {false, false, true}}, 1});
  regulated.push_back({{{true,  true,  false}, {false, true,  false}}, 3});

  Mode unregulated;
  unregulated.push_back({{{false, true,  false}, {false, true,  false}}, 1});
  unregulated.push_back({{{false, false, false}, {false, false, false}}, 1});

  modes.push_back(regulated);
  modes.push_back(unregulated);
}

void applyCurrentState() {
  Mode& traffic_modes = modes[currentMode];
  TrafficLight master = traffic_modes[currentState].directions[0];
  TrafficLight slave  = traffic_modes[currentState].directions[1];

  digitalWrite(RED_PIN, master.r);
  digitalWrite(YELLOW_PIN, master.y);
  digitalWrite(GREEN_PIN, master.g);

  char msg[10];
  snprintf(msg, sizeof(msg), "%d%d%d", slave.r, slave.y, slave.g);
  client.publish("outTopic", msg);
}

void switchMode() {
  currentMode = (currentMode + 1) % modes.size();
  currentState = 0;
  lastStepTime = millis();
  applyCurrentState();
}

void checkButton() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonReading) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW) { // Нажатие (LOW на FLASH кнопке)
       switchMode();
       lastButtonReading = HIGH; // Сброс, чтобы не залипало
       while(digitalRead(BUTTON_PIN) == LOW); // Ждем отпускания
    }
  }
  lastButtonReading = reading;
}

void setup() {
  Serial.begin(115200);
  pinMode(RED_PIN, OUTPUT); pinMode(YELLOW_PIN, OUTPUT); pinMode(GREEN_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  setupTrafficLogic();
  WiFi.begin(ssid, password);
  client.setServer(mqtt_server, 1883);
  applyCurrentState();
}

void loop() {
  // Неблокирующий реконнект
  if (!client.connected() && (millis() - lastMqttAttempt > 5000)) {
    lastMqttAttempt = millis();
    client.connect("ESP8266Traffic");
  }

  client.loop();
  checkButton();

  if (millis() - lastStepTime >= (unsigned long)modes[currentMode][currentState].duration * 1000) {
    currentState = (currentState + 1) % modes[currentMode].size();
    lastStepTime = millis();
    applyCurrentState();
  }
}
