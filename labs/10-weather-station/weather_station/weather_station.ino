#include "secrets.h"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SimpleDHT.h>
#include <EEPROM.h>
#include <math.h>

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>

#define CONFIG_BTN_PIN 0  // button
#define DHTPIN 2          // D4
#define RELAY_PIN 5       // D1

#define STA_SSID WIFI_SSID
#define STA_PSK WIFI_PASSWORD
#define MQTT_SERVER "broker.emqx.io"
#define MQTT_PORT 1883


const char* TOPIC_TEMP = "weather/station/temperature";
const char* TOPIC_HUM = "weather/station/humidity";
const char* TOPIC_ALERT = "weather/station/alert";

const char* TOPIC_FAKE_TEMP = "weather/station/fake/temp";

const char* TOPIC_SET_TEMP_MIN = "weather/station/set/temp/min";
const char* TOPIC_SET_TEMP_MAX = "weather/station/set/temp/max";
const char* TOPIC_SET_MODE = "weather/station/set/mode";
const char* TOPIC_SET_RELAY = "weather/station/set/relay";

enum ControlMode {
  MODE_HEAT,
  MODE_COOL,
  MODE_AUTO
};

struct Config {
  char ssid[32];
  char pass[64];
  char server[64];
  uint16_t port;

  float temp_min;
  float temp_max;

  ControlMode mode;

  bool relayState;
};

Config cfg;

WiFiClient espClient;
PubSubClient mqtt(espClient);
SimpleDHT11 dht(DHTPIN);
ESP8266WebServer server(80);
bool apMode = false;

float lastTemp = 0;
float lastHum = 0;

bool useFakeSensor = false;
bool tempAlertState = false;

unsigned long lastPublish = 0;

const unsigned long PUBLISH_INTERVAL = 5000;

const float HYSTERESIS = 0.5;

void saveConfig() {
  EEPROM.begin(512);
  EEPROM.put(0, cfg);
  EEPROM.commit();
  EEPROM.end();
}

void resetConfig() {
  Serial.println("Reset config...");

  memset(&cfg, 0, sizeof(cfg));

  strcpy(cfg.ssid, STA_SSID);
  strcpy(cfg.pass, STA_PSK);

  strcpy(cfg.server, MQTT_SERVER);
  cfg.port = MQTT_PORT;

  cfg.temp_min = 20.0;
  cfg.temp_max = 25.0;

  cfg.mode = MODE_AUTO;
  cfg.relayState = false;

  saveConfig();

  Serial.println("Reset done");
}

void loadConfig() {
  EEPROM.begin(512);
  EEPROM.get(0, cfg);
  EEPROM.end();

  if (cfg.ssid[0] == '\0' || cfg.ssid[0] == 0xFF) {
    resetConfig();
  }
}

void startAP() {
  apMode = true;

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP_Config", "12345671");

  Serial.println("AP started: ESP_Config | 12345671\nIP: 192.168.4.1");

  server.on("/", []() {

    String html =
      "<form action='/save' method='POST'>"

      "<h3>WiFi</h3>"
      "SSID:<input name='ssid' value='" + String(cfg.ssid) + "'><br>"
      "PASS:<input name='pass' value='" + String(cfg.pass) + "'><br><br>"

      "<h3>MQTT</h3>"
      "Server:<input name='server' value='" + String(cfg.server) + "'><br>"
      "Port:<input name='port' value='" + String(cfg.port) + "'><br><br>"

      "<h3>Temperature</h3>"
      "Temp MIN:<input name='tmin' value='" + String(cfg.temp_min) + "'><br>"
      "Temp MAX:<input name='tmax' value='" + String(cfg.temp_max) + "'><br><br>"

      "<button type='submit'>Save</button>"
      "</form>";

    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {

    String s = server.arg("ssid");
    String p = server.arg("pass");

    String srv = server.arg("server");
    String prt = server.arg("port");

    String tmin = server.arg("tmin");
    String tmax = server.arg("tmax");

    s.toCharArray(cfg.ssid, 32);
    p.toCharArray(cfg.pass, 64);

    srv.toCharArray(cfg.server, 64);
    cfg.port = prt.toInt();

    cfg.temp_min = tmin.toFloat();
    cfg.temp_max = tmax.toFloat();

    saveConfig();

    server.send(200, "text/html", "Saved. Rebooting...");
    delay(1500);
    ESP.restart();
  });

  server.begin();

  while (apMode) {
    server.handleClient();

    if (digitalRead(CONFIG_BTN_PIN) == LOW) {
      delay(3000);
      resetConfig();
      ESP.restart();
    }
  }
}

void setupOTA() {

  ArduinoOTA.setHostname("ESP8266-Weather");

  // ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() {

    String type;

    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else
      type = "filesystem";

    Serial.println("OTA Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {

    Serial.println("\nOTA End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {

    Serial.printf(
      "OTA Progress: %u%%\r",
      (progress / (total / 100))
    );
  });

  ArduinoOTA.onError([](ota_error_t error) {

    Serial.printf("OTA Error[%u]: ", error);

    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");

    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");

    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");

    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");

    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  Serial.println("OTA Ready");
}

void setRelay(bool state) {

  cfg.relayState = state;

  digitalWrite(RELAY_PIN, state ? HIGH : LOW);

  mqtt.publish(
    "weather/station/relay/state",
    state ? "ON" : "OFF"
  );

  Serial.print("Relay: ");
  Serial.println(state ? "ON" : "OFF");
}

void controlRelay(float temp) {

  // Если температура в норме — выключаем реле
  if (
    cfg.relayState &&
    temp >= cfg.temp_min &&
    temp <= cfg.temp_max
  ) {
    setRelay(false);
    return;
  }

  switch (cfg.mode) {

    case MODE_HEAT:

      if (
        !cfg.relayState &&
        temp < cfg.temp_min - HYSTERESIS
      ) {
        setRelay(true);
      }

      break;

    case MODE_COOL:

      if (
        !cfg.relayState &&
        temp > cfg.temp_max + HYSTERESIS
      ) {
        setRelay(true);
      }

      break;

    case MODE_AUTO:

      if (
        !cfg.relayState &&
        (
          temp < cfg.temp_min - HYSTERESIS ||
          temp > cfg.temp_max + HYSTERESIS
        )
      ) {
        setRelay(true);
      }

      break;
  }
}

void checkButton() {
  if (digitalRead(CONFIG_BTN_PIN) == LOW) {
    delay(100);
    unsigned long t = millis();

    while (digitalRead(CONFIG_BTN_PIN) == LOW) {
      if (millis() - t > 2000) {
        Serial.println("Entering AP mode...");
        startAP();
        return;
      }
    }
  }
}

void checkAlerts(float temp) {

  bool newState =
    (temp < cfg.temp_min) ||
    (temp > cfg.temp_max);

  if (newState && !tempAlertState) {

    String msg =
      "Temperature alert: " + String(temp);

    mqtt.publish(TOPIC_ALERT, msg.c_str());

    Serial.println(msg);
  }

  tempAlertState = newState;
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {

  char buffer[len + 1];

  memcpy(buffer, payload, len);

  buffer[len] = '\0';

  String msg = String(buffer);

  msg.trim();

  Serial.print("MQTT [");
  Serial.print(topic);
  Serial.print("] = ");
  Serial.println(msg);

  if (strcmp(topic, TOPIC_SET_TEMP_MIN) == 0) {

    float value = msg.toFloat();

    if (value < cfg.temp_max) {

      cfg.temp_min = value;

      saveConfig();

      Serial.print("temp_min = ");
      Serial.println(cfg.temp_min);
    }
  }

  else if (strcmp(topic, TOPIC_SET_TEMP_MAX) == 0) {

    float value = msg.toFloat();

    if (value > cfg.temp_min) {

      cfg.temp_max = value;

      saveConfig();

      Serial.print("temp_max = ");
      Serial.println(cfg.temp_max);
    }
  }

  else if (strcmp(topic, TOPIC_SET_MODE) == 0) {

    msg.toLowerCase();

    if (msg == "heat") {

      cfg.mode = MODE_HEAT;

      Serial.println("MODE = HEAT");
    }

    else if (msg == "cool") {

      cfg.mode = MODE_COOL;

      Serial.println("MODE = COOL");
    }

    else if (msg == "auto") {

      cfg.mode = MODE_AUTO;

      Serial.println("MODE = AUTO");
    }

    saveConfig();
  }

  else if (strcmp(topic, TOPIC_SET_RELAY) == 0) {

    msg.toUpperCase();

    if (msg == "ON")
      setRelay(true);

    else if (msg == "OFF")
      setRelay(false);
  }

  else if (strcmp(topic, TOPIC_FAKE_TEMP) == 0) {

    lastTemp = msg.toFloat();

    useFakeSensor = true;

    Serial.print("FAKE TEMP = ");
    Serial.println(lastTemp);
  }
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid, cfg.pass);

  Serial.print("Connecting WiFi");

  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) {
    delay(500);
    Serial.print(".");
    t++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    return true;
  }

  Serial.println("\nWiFi FAILED");
  return false;
}

void reconnectMQTT() {

  while (!mqtt.connected()) {

    String clientId =
      "ESP8266-" + String(ESP.getChipId());

    Serial.println("Connecting MQTT...");

    if (mqtt.connect(clientId.c_str())) {

      Serial.println("MQTT connected");

      mqtt.subscribe(TOPIC_SET_TEMP_MIN);
      mqtt.subscribe(TOPIC_SET_TEMP_MAX);

      mqtt.subscribe(TOPIC_SET_MODE);

      mqtt.subscribe(TOPIC_SET_RELAY);

      mqtt.subscribe(TOPIC_FAKE_TEMP);
    } else {

      Serial.print("MQTT failed: ");

      Serial.println(mqtt.state());

      delay(3000);
    }
  }
}

void setup() {

  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);

  digitalWrite(RELAY_PIN, LOW);

  loadConfig();

  if (!connectWiFi()) {
    startAP();
    return;
  }

  setupOTA();

  mqtt.setServer(cfg.server, cfg.port);
  mqtt.setCallback(mqttCallback);

  Serial.println("SYSTEM STARTED");
}

void loop() {

  ArduinoOTA.handle();

  checkButton();

  if (WiFi.status() != WL_CONNECTED) {
    if (!connectWiFi()) {
      startAP();
      return;
    }
  }

  if (!mqtt.connected()) {
    reconnectMQTT();
  }

  mqtt.loop();

  if (millis() - lastPublish > PUBLISH_INTERVAL) {

    lastPublish = millis();

    byte temp = 0;
    byte hum = 0;

    if (useFakeSensor) {

      String t = String(lastTemp);

      mqtt.publish(TOPIC_TEMP, t.c_str());

      checkAlerts(lastTemp);

      controlRelay(lastTemp);
    }

    else {

      int err = dht.read(&temp, &hum, NULL);

      if (err == SimpleDHTErrSuccess) {

        lastTemp = temp;
        lastHum = hum;

        String t = String(lastTemp);
        String h = String(lastHum);

        mqtt.publish(TOPIC_TEMP, t.c_str());
        mqtt.publish(TOPIC_HUM, h.c_str());

        Serial.println("=================");
        Serial.print("TEMP: ");
        Serial.println(lastTemp);

        Serial.print("HUM : ");
        Serial.println(lastHum);
        Serial.println("=================");

        checkAlerts(lastTemp);

        controlRelay(lastTemp);

      } else {

        Serial.println("DHT ERROR");
      }
    }
  }

  delay(100);
}
