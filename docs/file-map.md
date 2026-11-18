# Соответствие исходных и новых имён

| Исходный путь | Новый путь |
| --- | --- |
| `lab2_wifi_led/lab2_wifi_led.ino` | `labs/02-wifi-led/wifi_led/wifi_led.ino` |
| `lab3_dht11/lab3_dht11.ino` | `labs/03-dht11-http/dht11_http/dht11_http.ino` |
| `4lab_master/4lab_master.ino` | `labs/04-traffic-light-http/traffic_light_master/traffic_light_master.ino` |
| `4lab_slave/4lab_slave.ino` | `labs/04-traffic-light-http/traffic_light_slave/traffic_light_slave.ino` |
| `6lab_master/6lab_master.ino` | `labs/06-traffic-light-mqtt/traffic_light_master/traffic_light_master.ino` |
| `lab6_slave/lab6_slave.ino` | `labs/06-traffic-light-mqtt/traffic_light_slave/traffic_light_slave.ino` |
| `10lab_weather/10lab_weather.ino` | `labs/10-weather-station/weather_station/weather_station.ino` |
| `lab12.py` | `labs/12-tuya-device/device.py` |
| `lab13_api.py` | `labs/13-tuya-cloud-api/control.py` |
| `vk_server.py` | `integrations/vk-mqtt-bot/bot.py` |
| `ota_server/` | `ota/server/` |
| Исходный OTA-скетч `esp8266_ota_update/` | `ota/esp8266_ota_update/` |
| Исходный вариант `ota_server_index.js` | `ota/firmware-only-server/index.js` |
| `storage.json` | `labs/12-tuya-device/storage.json` — только локально, исключён из Git |

Локальные зависимости `libraries/` сохранены в корне Arduino sketchbook и исключены из Git. Обе папки `node_modules/` перенесены вместе со своими серверами и также исключены. Исходные бинарные файлы остались локально в каталогах OTA, но не публикуются.
