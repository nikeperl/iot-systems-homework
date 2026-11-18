/**
 * ============================================================
 * Сервер прошивок для ESP8266 OTA-обновления (Только прошивка)
 * ============================================================
 *
 * Структура каталогов:
 *   firmware-only-server/
 *   ├── index.js          ← этот файл
 *   └── ../server/firmware/
 *       └── firmware.bin  ← скомпилированная прошивка (.bin)
 */

'use strict';

const express = require('express');
const path    = require('path');
const fs      = require('fs');
const os      = require('os');

const app = express();

// ─── Конфигурация ────────────────────────────────────────────
const PORT = 3000;

// Актуальная версия прошивки, которая лежит в папке firmware
const LATEST_FIRMWARE_VERSION = '3.0';

// Путь к бинарному файлу прошивки
const FIRMWARE_PATH = path.join(__dirname, '../server/firmware', 'firmware.bin');

// ─── Вспомогательные функции ─────────────────────────────────
function getLocalIPs() {
  const results = {};
  const nets = os.networkInterfaces();
  for (const name of Object.keys(nets)) {
    for (const iface of nets[name]) {
      if (iface.family === 'IPv4' && !iface.internal) {
        (results[name] = results[name] ||[]).push(iface.address);
      }
    }
  }
  return results;
}

function needsUpdate(deviceVersionStr, latestVersionStr) {
  const dv = parseFloat(deviceVersionStr);
  const lv = parseFloat(latestVersionStr);
  if (isNaN(dv)) return true;
  return dv < lv;
}

// ─── Логирование запросов ────────────────────────────────────
app.use((req, res, next) => {
  const chip = req.header('x-esp8266-chip-id') || 'неизвестно';
  const ver  = req.header('x-esp8266-version') || '—';
  console.log(`[${new Date().toISOString()}] ${req.method} ${req.path} | chip=${chip} ver=${ver}`);
  next();
});

// ─── Эндпоинты ───────────────────────────────────────────────
app.get('/', (req, res) => {
  res.json({
    status: 'OK',
    message: 'ESP8266 OTA Сервер работает',
    endpoints: {
      '/version': 'Текущая версия',
      '/update':  'Обновление прошивки'
    }
  });
});

app.get('/version', (req, res) => {
  res.json({ firmware: LATEST_FIRMWARE_VERSION });
});

app.get('/update', (req, res) => {
  const deviceVersion = req.header('x-esp8266-version');

  if (!needsUpdate(deviceVersion, LATEST_FIRMWARE_VERSION)) {
    console.log(`[/update] Версия устройства (${deviceVersion}) актуальна. Обновление не нужно.`);
    return res.status(304).send('No update needed');
  }

  if (!fs.existsSync(FIRMWARE_PATH)) {
    console.error('[/update] ОШИБКА: файл прошивки не найден:', FIRMWARE_PATH);
    return res.status(404).send('Firmware file not found on server');
  }

  console.log(`[/update] Отправка прошивки v${LATEST_FIRMWARE_VERSION} → устройству v${deviceVersion}`);

  // Используем sendFile вместо download — это более правильно для железа
  res.status(200).sendFile(FIRMWARE_PATH, (err) => {
    if (err) {
      console.error('[/update] Ошибка при отправке файла:', err.message);
    } else {
      console.log('[/update] Прошивка успешно отправлена.');
    }
  });
});

// ─── Запуск сервера ──────────────────────────────────────────
app.listen(PORT, () => {
  console.log('==============================================');
  console.log('  ESP8266 OTA Сервер прошивок запущен');
  console.log('==============================================');
  console.log(`  Версия прошивки на сервере: ${LATEST_FIRMWARE_VERSION}`);
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
  console.log(`  Файл прошивки: ${FIRMWARE_PATH}`);
  if (!fs.existsSync(FIRMWARE_PATH)) {
    console.warn('  ⚠ ВНИМАНИЕ: файл прошивки НЕ НАЙДЕН!');
  }
  console.log('==============================================');
});
