#include "secrets.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;


const char* serverIP = "192.168.68.XX";

int LED = 2;
int BUTTON = 0;

WiFiServer server(80);

bool ledState = false;
bool lastButtonState = HIGH;

void sendCommand(String cmd) {
  WiFiClient client;
  HTTPClient http;
  String url = "http://" + String(serverIP) + "/LED=" + cmd;
  http.begin(client, url);
  int code = http.GET();
  Serial.println("Sent: " + cmd + " | Response: " + String(code));
  http.end();
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 2000) { client.stop(); return; }
    delay(1);
  }

  String request = client.readStringUntil('\r');
  Serial.println("Received: " + request);

  if (request.indexOf("/LED=ON") != -1) {
    digitalWrite(LED, LOW);
    ledState = true;
    Serial.println("LED ON");
  }
  if (request.indexOf("/LED=OFF") != -1) {
    digitalWrite(LED, HIGH);
    ledState = false;
    Serial.println("LED OFF");
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println("");
  client.println("OK");
  client.stop();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  pinMode(BUTTON, INPUT_PULLUP);

  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("My IP: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("Server started");
}

void loop() {
  handleClient();

  bool buttonState = digitalRead(BUTTON);
  if (buttonState == LOW && lastButtonState == HIGH) {
    delay(50); // антидребезг
    ledState = !ledState;
    sendCommand(ledState ? "ON" : "OFF");
  }
  lastButtonState = buttonState;

  delay(10);
}
