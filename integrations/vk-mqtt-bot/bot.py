"""
ОБЪЕДИНЁННЫЙ VK-БОТ: Умный дом (Задание 1 и Задание 2)
Поддерживает главное меню, переключение режимов и кнопки (VK Keyboard).
"""

import os
import vk_api
from vk_api.longpoll import VkLongPoll, VkEventType
from vk_api.keyboard import VkKeyboard, VkKeyboardColor
import paho.mqtt.client as mqtt
import random
import logging
import threading

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger(__name__)

# ============================================================
# НАСТРОЙКИ — ЗАПОЛНИ СВОИ ЗНАЧЕНИЯ
# ============================================================
VK_GROUP_TOKEN = os.environ["VK_GROUP_TOKEN"]
ADMIN_VK_ID = int(os.environ["ADMIN_VK_ID"])

MQTT_HOST       = "broker.emqx.io"
MQTT_PORT       = 1883

# Топики Светодиода (Задание 1)
TOPIC_LED_CMD   = "vk/led/command"
TOPIC_LED_STATE = "vk/led/state"

# Топики Метеостанции (Задание 2)
TOPIC_WEATHER_CMD    = "vk/station/command"
TOPIC_WEATHER_NOTIFY = "vk/station/notify"
# ============================================================

# Инициализация VK
vk_session = vk_api.VkApi(token=VK_GROUP_TOKEN)
vk         = vk_session.get_api()
longpoll   = VkLongPoll(vk_session)

# Состояние пользователя: "main", "led" или "weather"
user_state = "main"

def send_vk(user_id: int, text: str, keyboard=None):
    """Отправка сообщения в ВК (с поддержкой кнопок)"""
    params = {
        "user_id": user_id,
        "message": text,
        "random_id": random.randint(1, 2**31)
    }
    if keyboard is not None:
        params["keyboard"] = keyboard.get_keyboard()

    vk.messages.send(**params)
    log.info("VK → %s", text.replace('\n', ' ')[:60])

# ================= КЛАВИАТУРЫ =================
def get_main_keyboard():
    kb = VkKeyboard(one_time=False)
    kb.add_button("Задание 1: Светодиод", color=VkKeyboardColor.PRIMARY)
    kb.add_line()
    kb.add_button("Задание 2: Метеостанция", color=VkKeyboardColor.PRIMARY)
    return kb

def get_led_keyboard():
    kb = VkKeyboard(one_time=False)
    kb.add_button("on", color=VkKeyboardColor.POSITIVE)
    kb.add_button("off", color=VkKeyboardColor.NEGATIVE)
    kb.add_button("toggle", color=VkKeyboardColor.SECONDARY)
    kb.add_line()
    kb.add_button("state", color=VkKeyboardColor.PRIMARY)
    kb.add_line()
    kb.add_button("Назад", color=VkKeyboardColor.SECONDARY)
    return kb

def get_weather_keyboard():
    kb = VkKeyboard(one_time=False)
    kb.add_button("state", color=VkKeyboardColor.PRIMARY)
    kb.add_button("relay auto", color=VkKeyboardColor.SECONDARY)
    kb.add_line()
    kb.add_button("relay on", color=VkKeyboardColor.POSITIVE)
    kb.add_button("relay off", color=VkKeyboardColor.NEGATIVE)
    kb.add_line()
    kb.add_button("mode heat", color=VkKeyboardColor.NEGATIVE)
    kb.add_button("mode cool", color=VkKeyboardColor.PRIMARY)
    kb.add_line()
    kb.add_button("Назад", color=VkKeyboardColor.SECONDARY)
    return kb

# ================= MQTT ЛОГИКА =================
def on_mqtt_connect(client, userdata, flags, rc):
    log.info("MQTT connected rc=%d", rc)
    client.subscribe(TOPIC_LED_STATE)
    client.subscribe(TOPIC_WEATHER_NOTIFY)

def on_mqtt_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode("utf-8", errors="replace").strip()

    if topic == TOPIC_LED_STATE:
        # Ответ от Задания 1
        send_vk(ADMIN_VK_ID, f"💡 Состояние светодиода: {payload}")

    elif topic == TOPIC_WEATHER_NOTIFY:
        # Ответ или тревога от Задания 2
        send_vk(ADMIN_VK_ID, payload)

mqtt_client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION1,
    client_id="VKBot-Unified"
)
mqtt_client.on_connect = on_mqtt_connect
mqtt_client.on_message = on_mqtt_message
mqtt_client.connect(MQTT_HOST, MQTT_PORT, 60)
mqtt_client.loop_start()

# ================= ВАЛИДАЦИЯ КОМАНД МЕТЕО =================
ALLOWED_WEATHER_CMDS = {
    "state", "relay on", "relay off", "relay auto", "mode heat", "mode cool"
}

def is_weather_set_command(text: str) -> bool:
    """Проверяет ручной ввод лимитов: set temp min 20"""
    parts = text.split()
    if len(parts) == 4 and parts[0] == "set" and parts[1] in ("temp", "hum") and parts[2] in ("min", "max"):
        try:
            float(parts[3])
            return True
        except ValueError:
            pass
    return False

# ================= VK ЛОГИКА =================
def handle_vk_message(event):
    global user_state
    text = event.text.strip().lower()
    uid  = event.user_id

    # Защита: работает только для Админа
    if uid != ADMIN_VK_ID:
        send_vk(uid, "⛔ У вас нет доступа к умному дому.")
        return

    # Глобальная команда возврата в меню
    if text in ("назад", "меню", "/start", "старт", "help", "помощь"):
        user_state = "main"
        send_vk(uid, "🏠 Главное меню. Выберите устройство:", keyboard=get_main_keyboard())
        return

    # ----- ЛОГИКА ГЛАВНОГО МЕНЮ -----
    if user_state == "main":
        if "задание 1" in text or "светодиод" in text:
            user_state = "led"
            send_vk(uid, "💡 Режим: Управление светодиодом.\nВыберите команду или введите вручную:", keyboard=get_led_keyboard())
        elif "задание 2" in text or "метео" in text:
            user_state = "weather"
            send_vk(uid, "🌦 Режим: Метеостанция.\nВыберите команду или введите пороги (например: set temp min 20):", keyboard=get_weather_keyboard())
        else:
            send_vk(uid, "Пожалуйста, используйте кнопки на клавиатуре.", keyboard=get_main_keyboard())

    # ----- ЛОГИКА СВЕТОДИОДА (ЗАДАНИЕ 1) -----
    elif user_state == "led":
        if text in ("on", "off", "toggle", "state"):
            mqtt_client.publish(TOPIC_LED_CMD, text)
            send_vk(uid, f"📤 На ESP8266 отправлено: {text}")
        else:
            send_vk(uid, "❓ Неизвестная команда для LED.\nИспользуйте кнопки или напишите 'назад'.", keyboard=get_led_keyboard())

    # ----- ЛОГИКА МЕТЕОСТАНЦИИ (ЗАДАНИЕ 2) -----
    elif user_state == "weather":
        if text in ALLOWED_WEATHER_CMDS or is_weather_set_command(text):
            mqtt_client.publish(TOPIC_WEATHER_CMD, text)
            send_vk(uid, f"📤 Команда отправлена на метеостанцию: {text}")
        else:
            send_vk(uid, "❓ Неизвестная команда станции.\nИспользуйте кнопки или введите порог (set temp min 18).", keyboard=get_weather_keyboard())

log.info("ОБЪЕДИНЕННЫЙ VK-БОТ ЗАПУЩЕН! Жду сообщений...")
send_vk(ADMIN_VK_ID, "🚀 Бот запущен! Сервер готов к работе.", keyboard=get_main_keyboard())

# Бесконечный цикл прослушивания ВК
for event in longpoll.listen():
    if event.type == VkEventType.MESSAGE_NEW and event.to_me:
        try:
            handle_vk_message(event)
        except Exception as e:
            log.error("Ошибка при обработке сообщения: %s", e)
