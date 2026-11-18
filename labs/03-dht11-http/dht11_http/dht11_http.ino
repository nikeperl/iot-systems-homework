#include "secrets.h"

#include <DHT11.h>
#include <ESP8266WiFi.h>

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

DHT11 dht11(D4);

WiFiServer server(80);

String getTemperatureAndHumidityJSON() {
    int temperature = 0;
    int humidity = 0;

    int result = dht11.readTemperatureHumidity(temperature, humidity);

    if (result == 0) {
        String json = "{";
        json += "\"temperature\":";
        json += temperature;
        json += ",\"humidity\":";
        json += humidity;
        json += "}";
        return json;
    } else {
        return String("{\"error\":\"") + DHT11::getErrorString(result) + "\"}";
    }
}

void handleClient() {
    WiFiClient client = server.available();
    if (!client) return;

    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 2000) {
            client.stop();
            return;
        }
        delay(1);
    }

    String request = client.readStringUntil('\r');
    Serial.println("Received: " + request);

    String response = "{}";

    if (request.indexOf("GET /get_temp") != -1) {
        response = getTemperatureAndHumidityJSON();
        Serial.println(response); // лог в Serial
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println(response);

    client.stop();
}

void setup() {
    Serial.begin(115200);

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
}
