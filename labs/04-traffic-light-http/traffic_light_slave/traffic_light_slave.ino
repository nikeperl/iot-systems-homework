#include "secrets.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const int RED_PIN    = 14; // D5
const int YELLOW_PIN = 12; // D6
const int GREEN_PIN  = 13; // D7

ESP8266WebServer server(80);

// 👉 HTTP обработчик вместо MQTT callback
void handleState() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No data");
    return;
  }

  String msg = server.arg("plain");

  if (msg.length() < 3) {
    server.send(400, "text/plain", "Invalid");
    return;
  }

  bool r = msg[0] == '1';
  bool y = msg[1] == '1';
  bool g = msg[2] == '1';

  digitalWrite(RED_PIN, r);
  digitalWrite(YELLOW_PIN, y);
  digitalWrite(GREEN_PIN, g);

  server.send(200, "text/plain", "OK");
}

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RED_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);

  digitalWrite(RED_PIN, LOW);
  digitalWrite(YELLOW_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);

  setup_wifi();

  // 👉 HTTP endpoint
  server.on("/state", HTTP_POST, handleState);

  server.begin();
}

void loop() {
  server.handleClient();
}
