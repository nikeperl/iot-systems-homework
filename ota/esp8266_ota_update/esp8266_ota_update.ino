#include "secrets.h"

/*
 * ============================================================
 * ESP8266 — Автоматическое OTA-обновление с "сервера прошивок"
 * Реализует:
 *   1. Проверку наличия новой версии прошивки (через HTTP-заголовок)
 *   2. Автоматическую загрузку и применение обновления
 *   3. Проверку и обновление файловой системы (LittleFS)
 *   4. Повторные попытки с задержкой при ошибках
 * ============================================================
 *
 * Зависимости (Arduino IDE, Board: esp8266 by ESP8266 Community):
 *   - ESP8266WiFi        (встроена)
 *   - ESP8266WiFiMulti   (встроена)
 *   - ESP8266HTTPClient  (встроена)
 *   - ESP8266httpUpdate  (встроена)
 *   - LittleFS           (встроена)
 *
 * Структура файловой системы (data/):
 *   /version.txt  — текущая версия файловой системы (например, "1.0")
 *
 * Настройка сервера: см. файл ../server/index.js
 * ============================================================
 */

#include <Arduino.h>
#include <LittleFS.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

// ─── Настройки Wi-Fi ────────────────────────────────────────



// ─── Настройки сервера прошивок ─────────────────────────────
#define OTA_SERVER_HOST  "10.140.180.97"   // IP или hostname сервера
#define OTA_SERVER_PORT  3000              // Порт сервера

// Эндпоинты на сервере:
//   GET /update    — проверка и загрузка новой прошивки
//   GET /updateFS  — проверка и загрузка нового образа FS
#define FW_ENDPOINT  "/update"
#define FS_ENDPOINT  "/updateFS"

// ─── Версия текущей прошивки ────────────────────────────────
// ВАЖНО: обновляйте эту строку при каждой сборке!
#define FIRMWARE_VERSION "3.0"

// ─── Интервал проверки обновлений ───────────────────────────
// 5 минут между проверками (в миллисекундах)
#define CHECK_INTERVAL_MS  (5UL * 60UL * 1000UL)

// ─── Задержка перед перезагрузкой после обновления ──────────
#define REBOOT_DELAY_MS  2000

// ─── Задержка перед повторной попыткой при ошибке ───────────
#define RETRY_DELAY_MS  10000

// ────────────────────────────────────────────────────────────

ESP8266WiFiMulti wifiMulti;

// Версия файловой системы, считывается из /version.txt
String fsVersion = "0.0";

// Временная метка последней проверки
unsigned long lastCheckTime = 0;
bool firstRun = true;

// ─── Чтение версии FS из файла ──────────────────────────────
void readFsVersion() {
  File f = LittleFS.open("/version.txt", "r");
  if (f) {
    fsVersion = f.readStringUntil('\n');
    fsVersion.trim();
    f.close();
    Serial.print(F("[FS] Версия файловой системы: "));
    Serial.println(fsVersion);
  } else {
    Serial.println(F("[FS] /version.txt не найден, используется версия 0.0"));
  }
}

// ─── Колбэки прогресса обновления ───────────────────────────
void onUpdateStart() {
  Serial.println(F("\n[OTA] Начало загрузки обновления..."));
}

void onUpdateFinish() {
  Serial.println(F("[OTA] Загрузка завершена."));
}

void onUpdateProgress(int cur, int total) {
  // Показываем прогресс каждые ~10%
  static int lastPct = -1;
  int pct = (total > 0) ? (cur * 100 / total) : 0;
  if (pct / 10 != lastPct / 10) {
    lastPct = pct;
    Serial.printf("[OTA] Прогресс: %d / %d байт (%d%%)\n", cur, total, pct);
  }
}

void onUpdateError(int err) {
  Serial.printf("[OTA] Ошибка обновления, код: %d\n", err);
}

// ─── Обновление прошивки с проверкой версии ─────────────────
bool checkAndUpdateFirmware() {
  WiFiClient client;

  ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
  ESPhttpUpdate.onStart(onUpdateStart);
  ESPhttpUpdate.onEnd(onUpdateFinish);
  ESPhttpUpdate.onProgress(onUpdateProgress);
  ESPhttpUpdate.onError(onUpdateError);
  ESPhttpUpdate.rebootOnUpdate(false); // управляем перезагрузкой вручную

  Serial.println(F("\n[FW] Проверка обновления прошивки..."));
  Serial.printf("[FW] Текущая версия: %s\n", FIRMWARE_VERSION);

  // Вызов с передачей версии в заголовке x-esp8266-version
  t_httpUpdate_return ret = ESPhttpUpdate.update(
    client,
    OTA_SERVER_HOST,
    OTA_SERVER_PORT,
    FW_ENDPOINT,
    FIRMWARE_VERSION
  );

  switch (ret) {
    case HTTP_UPDATE_OK:
      Serial.println(F("[FW] Прошивка успешно обновлена! Перезагрузка..."));
      delay(REBOOT_DELAY_MS);
      ESP.restart();
      return true; // не достигнем, но для ясности

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println(F("[FW] Прошивка актуальна, обновление не требуется."));
      return false;

    case HTTP_UPDATE_FAILED:
      Serial.printf("[FW] Ошибка: (%d) %s\n",
        ESPhttpUpdate.getLastError(),
        ESPhttpUpdate.getLastErrorString().c_str());
      return false;

    default:
      return false;
  }
}

// ─── Обновление файловой системы с проверкой версии ─────────
bool checkAndUpdateFilesystem() {
  WiFiClient client;

  ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
  ESPhttpUpdate.onStart(onUpdateStart);
  ESPhttpUpdate.onEnd(onUpdateFinish);
  ESPhttpUpdate.onProgress(onUpdateProgress);
  ESPhttpUpdate.onError(onUpdateError);
  ESPhttpUpdate.rebootOnUpdate(false);

  Serial.println(F("\n[FS] Проверка обновления файловой системы..."));
  Serial.print(F("[FS] Текущая версия FS: "));
  Serial.println(fsVersion);

  // Вызов updateFS — обновляет образ LittleFS/SPIFFS
  t_httpUpdate_return ret = ESPhttpUpdate.updateFS(
    client,
    String("http://") + OTA_SERVER_HOST + ":" + OTA_SERVER_PORT + FS_ENDPOINT,
    fsVersion
  );

  switch (ret) {
    case HTTP_UPDATE_OK:
      Serial.println(F("[FS] Файловая система обновлена! Перезагрузка..."));
      delay(REBOOT_DELAY_MS);
      ESP.restart();
      return true;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println(F("[FS] Файловая система актуальна."));
      return false;

    case HTTP_UPDATE_FAILED:
      Serial.printf("[FS] Ошибка: (%d) %s\n",
        ESPhttpUpdate.getLastError(),
        ESPhttpUpdate.getLastErrorString().c_str());
      return false;

    default:
      return false;
  }
}

// ─── Подключение к Wi-Fi ─────────────────────────────────────
bool connectWiFi() {
  if (wifiMulti.run() == WL_CONNECTED) {
    return true;
  }
  Serial.print(F("[WiFi] Подключение"));
  unsigned long start = millis();
  while (wifiMulti.run() != WL_CONNECTED) {
    if (millis() - start > 15000) {
      Serial.println(F("\n[WiFi] Таймаут подключения!"));
      return false;
    }
    Serial.print('.');
    delay(500);
  }
  Serial.println();
  Serial.print(F("[WiFi] Подключено! IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

// ────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("==============================================="));
  Serial.println(F("  ESP8266 OTA Update Demo"));
  Serial.printf ("  Версия прошивки: %s\n", FIRMWARE_VERSION);
  Serial.println(F("==============================================="));

  // Инициализация файловой системы
  Serial.print(F("[FS] Инициализация LittleFS... "));
  if (LittleFS.begin()) {
    Serial.println(F("OK"));
    readFsVersion();
  } else {
    Serial.println(F("ОШИБКА! Файловая система не смонтирована."));
  }

  // Настройка Wi-Fi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  Serial.println(F("[Setup] Инициализация завершена."));
}

// ────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Проверяем обновления при первом запуске или по истечении интервала
  bool timeToCheck = firstRun || (now - lastCheckTime >= CHECK_INTERVAL_MS);

  if (timeToCheck) {
    firstRun = false;
    lastCheckTime = now;

    Serial.println(F("\n--- Плановая проверка обновлений ---"));

    if (!connectWiFi()) {
      Serial.println(F("[WiFi] Нет соединения, пропускаем проверку."));
      delay(RETRY_DELAY_MS);
      return;
    }

    // Сначала обновляем файловую систему (если нужно — перезагружается)
    checkAndUpdateFilesystem();

    // Затем обновляем прошивку (если нужно — перезагружается)
    checkAndUpdateFirmware();

    Serial.println(F("--- Проверка завершена ---"));
    Serial.printf("[Loop] Следующая проверка через %lu мин.\n",
                  CHECK_INTERVAL_MS / 60000UL);
  }

  // ─── Здесь размещается основная логика вашего приложения ───
  // Например: чтение датчиков, управление реле, отправка данных и т.д.
  delay(1000);
}
