#include "secrets.h"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* mqtt_server = "192.168.68.73";

const int RED_PIN    = 5; // D1
const int YELLOW_PIN = 4; // D2
const int GREEN_PIN  = 0; // D3

WiFiClient espClient;
PubSubClient client(espClient);

// -------------- Callback при получении сообщения --------------
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  if (length < 3) {
    Serial.println("Invalid payload length");
    return;
  }

  bool r = (payload[0] == '1');
  bool y = (payload[1] == '1');
  bool g = (payload[2] == '1');

  Serial.printf("R=%d Y=%d G=%d\n", r, y, g);

  digitalWrite(RED_PIN,    r ? HIGH : LOW);
  digitalWrite(YELLOW_PIN, y ? HIGH : LOW);
  digitalWrite(GREEN_PIN,  g ? HIGH : LOW);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Slave-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe("outTopic");
      Serial.println("Subscribed to outTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RED_PIN,    OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN,  OUTPUT);

  // Начальное состояние — всё выключено
  digitalWrite(RED_PIN,    LOW);
  digitalWrite(YELLOW_PIN, LOW);
  digitalWrite(GREEN_PIN,  LOW);

  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
