/**
 * ============================================================
 * Сервер прошивок для ESP8266 OTA-обновления
 * ============================================================
 *
 * Установка и запуск:
 *   npm init -y
 *   npm install express
 *   node index.js
 *
 * Структура каталогов:
 *   ota_server/
 *   ├── index.js          ← этот файл
 *   ├── firmware/
 *   │   └── firmware.bin  ← скомпилированная прошивка (.bin)
 *   └── filesystem/
 *       └── filesystem.bin ← образ файловой системы (.mklittlefs.bin)
 *
 * Как получить firmware.bin:
 *   Arduino IDE → Скетч → Экспорт скомпилированного бинарного файла
 *   Переименуйте полученный .bin в firmware.bin и положите в папку
firmware/
 *
 * Как получить filesystem.bin:
 *   Arduino IDE → Инструменты → LittleFS Data Upload (плагин)
 *   Скопируйте .mklittlefs.bin из temp-папки (путь будет в консоли)
 *   Переименуйте в filesystem.bin и положите в папку filesystem/
 *
 * Эндпоинты:
 *   GET /              — проверка работоспособности сервера
 *   GET /version       — текущие версии прошивки и FS (JSON)
 *   GET /update        — обновление прошивки (с проверкой версии)
 *   GET /updateFS      — обновление файловой системы (с проверкой версии)
 * ============================================================
 */

'use strict';

const express = require('express');
const path    = require('path');
const fs      = require('fs');
const os      = require('os');

const app = express();

// ─── Конфигурация
// ────────────────────────────────────────────
const PORT = 3000;

// Актуальные версии, которые должны быть на устройстве
// Обновляйте эти значения при публикации новой прошивки/FS
const LATEST_FIRMWARE_VERSION    = '2.0';
const LATEST_FILESYSTEM_VERSION  = '2.0';

// Пути к бинарным файлам
const FIRMWARE_PATH   = path.join(__dirname, 'firmware',
'firmware.bin');
const FILESYSTEM_PATH = path.join(__dirname, 'filesystem',
'filesystem.bin');

// ─── Вспомогательная функция: получение всех IPv4-адресов ────
function getLocalIPs() {
  const results = {};
  const nets = os.networkInterfaces();
  for (const name of Object.keys(nets)) {
    for (const iface of nets[name]) {
      if (iface.family === 'IPv4' && !iface.internal) {
        (results[name] = results[name] || []).push(iface.address);
      }
    }
  }
  return results;
}

// ─── Вспомогательная функция: разбор версии из заголовка ─────
function parseVersion(header) {
  if (!header) return null;
  const v = parseFloat(header.trim());
  return isNaN(v) ? null : v;
}

// ─── Вспомогательная функция: сравнение версий (строки) ──────
// Возвращает true, если deviceVersion < latestVersion
function needsUpdate(deviceVersionStr, latestVersionStr) {
  const dv = parseFloat(deviceVersionStr);
  const lv = parseFloat(latestVersionStr);
  if (isNaN(dv)) return true;  // неизвестная версия → обновляем
  return dv < lv;
}

// ─── Логирование запросов
// ─────────────────────────────────────
app.use((req, res, next) => {
  const chip = req.header('x-esp8266-chip-id') || 'неизвестно';
  const ver  = req.header('x-esp8266-version')  || '—';
  console.log(`[${new Date().toISOString()}] ${req.method} ${req.path} |
chip=${chip} ver=${ver}`);
  next();
});

// ─── GET /
// ───────────────────────────────────────────────────
app.get('/', (req, res) => {
  res.json({
    status: 'OK',
    message: 'ESP8266 OTA Сервер прошивок работает',
    endpoints: {
      '/version':  'Текущие версии',
      '/update':   'Обновление прошивки',
      '/updateFS': 'Обновление файловой системы',
    }
  });
});

// ─── GET /version
// ────────────────────────────────────────────
// Возвращает актуальные версии в формате JSON.
// ESP8266 может опросить этот эндпоинт перед загрузкой.
app.get('/version', (req, res) => {
  res.json({
    firmware:    LATEST_FIRMWARE_VERSION,
    filesystem:  LATEST_FILESYSTEM_VERSION,
  });
});

// ─── GET /update
// ─────────────────────────────────────────────
// Эндпоинт обновления прошивки с контролем версии.
//
// ESP8266 автоматически передаёт версию в заголовке:
//   x-esp8266-version: <FIRMWARE_VERSION>
//
// Логика:
//   - Если версия устройства < LATEST_FIRMWARE_VERSION → отдаём .bin
// (200)
//   - Если версия актуальна → 304 Not Modified (обновление не нужно)
//   - Если файл не найден → 404
app.get('/update', (req, res) => {
  const deviceVersion = req.header('x-esp8266-version');

  // Дополнительная диагностика (полезно при отладке)
  console.log('[/update] Заголовки от ESP8266:');
  console.log('  chip-id:      ', req.header('x-esp8266-chip-id'));
  console.log('  sta-mac:      ', req.header('x-esp8266-sta-mac'));
  console.log('  free-space:   ', req.header('x-esp8266-free-space'));
  console.log('  sketch-size:  ', req.header('x-esp8266-sketch-size'));
  console.log('  sdk-version:  ', req.header('x-esp8266-sdk-version'));
  console.log('  mode:         ', req.header('x-esp8266-mode'));
  console.log('  version:      ', deviceVersion);

  if (!needsUpdate(deviceVersion, LATEST_FIRMWARE_VERSION)) {
    console.log(`[/update] Версия устройства (${deviceVersion}) актуальна.
Обновление не нужно.`);
    return res.status(304).send('No update needed');
  }

  if (!fs.existsSync(FIRMWARE_PATH)) {
    console.error('[/update] ОШИБКА: файл прошивки не найден:',
FIRMWARE_PATH);
    return res.status(404).send('Firmware file not found on server');
  }

  console.log(`[/update] Отправка прошивки v${LATEST_FIRMWARE_VERSION} →
устройству v${deviceVersion}`);
  res.status(200).download(FIRMWARE_PATH, 'firmware.bin', (err) => {
    if (err) {
      console.error('[/update] Ошибка при отправке файла:', err.message);
    } else {
      console.log('[/update] Прошивка успешно отправлена.');
    }
  });
});

// ─── GET /updateFS
// ───────────────────────────────────────────
// Эндпоинт обновления файловой системы с контролем версии.
//
// ESP8266 передаёт версию FS в заголовке x-esp8266-version
// (это значение, которое скетч явно передаёт при вызове updateFS).
//
// Логика:
//   - Если версия FS устройства < LATEST_FILESYSTEM_VERSION → отдаём
// .bin (200)
//   - Если версия актуальна → 304
//   - Если файл не найден → 404
app.get('/updateFS', (req, res) => {
  const deviceFsVersion = req.header('x-esp8266-version');

  console.log(`[/updateFS] Версия FS устройства: ${deviceFsVersion}`);

  if (!needsUpdate(deviceFsVersion, LATEST_FILESYSTEM_VERSION)) {
    console.log(`[/updateFS] FS актуальна (${deviceFsVersion}). Обновление
не нужно.`);
    return res.status(304).send('No FS update needed');
  }

  if (!fs.existsSync(FILESYSTEM_PATH)) {
    console.error('[/updateFS] ОШИБКА: образ FS не найден:',
FILESYSTEM_PATH);
    return res.status(404).send('Filesystem image not found on server');
  }

  console.log(`[/updateFS] Отправка образа FS
v${LATEST_FILESYSTEM_VERSION} → устройству v${deviceFsVersion}`);
  res.status(200).download(FILESYSTEM_PATH, 'filesystem.bin', (err) => {
    if (err) {
      console.error('[/updateFS] Ошибка при отправке файла:',
err.message);
    } else {
      console.log('[/updateFS] Образ FS успешно отправлен.');
    }
  });
});

// ─── Запуск сервера
// ──────────────────────────────────────────
app.listen(PORT, () => {
  console.log('==============================================');
  console.log('  ESP8266 OTA Сервер прошивок запущен');
  console.log('==============================================');
  console.log(`  Версия прошивки на сервере: ${LATEST_FIRMWARE_VERSION}`);
  console.log(`  Версия FS на сервере:
${LATEST_FILESYSTEM_VERSION}`);
  console.log('----------------------------------------------');
  console.log('  Доступные адреса:');

  const ips = getLocalIPs();
  if (Object.keys(ips).length === 0) {
    console.log(`  http://localhost:${PORT}`);
  } else {
    for (const [iface, addrs] of Object.entries(ips)) {
      for (const addr of addrs) {
        console.log(`  [${iface}] http://${addr}:${PORT}`);
      }
    }
  }

  console.log('----------------------------------------------');
  console.log('  Убедитесь, что файлы существуют:');
  console.log(`  firmware:   ${FIRMWARE_PATH}`);
  console.log(`  filesystem: ${FILESYSTEM_PATH}`);

  if (!fs.existsSync(FIRMWARE_PATH)) {
    console.warn('  ⚠ ВНИМАНИЕ: файл прошивки НЕ НАЙДЕН!');
  }
  if (!fs.existsSync(FILESYSTEM_PATH)) {
    console.warn('  ⚠ ВНИМАНИЕ: образ файловой системы НЕ НАЙДЕН!');
  }

  console.log('==============================================');
});
