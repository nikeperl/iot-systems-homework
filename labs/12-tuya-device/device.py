import os
import time
import coloredlogs
from tuyalinksdk.client import TuyaClient
from tuyalinksdk.console_qrcode import qrcode_generate

# Настройка красивого вывода логов в консоль
coloredlogs.install(level='DEBUG')

# Инициализация клиента: это превращает твой компьютер в устройство
client = TuyaClient(productid=os.environ["TUYA_PRODUCT_ID"],
                    uuid=os.environ["TUYA_UUID"],
                    authkey=os.environ["TUYA_AUTH_KEY"])

# Что делать, когда устройство подключилось к облаку
def on_connected():
    print('Устройство успешно подключилось к облаку!')

# Что делать, когда облако просит привязать устройство (генерация QR)
def on_qrcode(url):
    print('Пожалуйста, отсканируй QR-код в приложении Tuya:')
    qrcode_generate(url)

# Что делать, если пришел сигнал "Сброс к заводским настройкам"
def on_reset(data):
    print('Устройство сброшено:', data)

def on_dps(dps):
    print('Получены данные:', dps)

    # 1. Если пришла команда, мы должны отправить подтверждение (Report)
    # Попробуем отправить данные так, чтобы TuyaLink понял, что это изменение свойств

    # Вместо простого push_dps, попробуем отправить структуру,
    # которую TuyaLink точно считает за "обновление состояния"

    # Если пришел '104', отправляем и '104' (для надежности) и 'a'
    if '104' in dps:
        payload = {'104': dps['104'], 'a': dps['104']}
        client.push_dps(payload)
        print("Отправили Report (104 + a):", payload)

# Привязка функций к событиям SDK
client.on_connected = on_connected
client.on_qrcode = on_qrcode
client.on_reset = on_reset
client.on_dps = on_dps

# Запуск
client.connect()
client.loop_start()

# Основной цикл программы
try:
    while True:
        # Здесь можно добавить свою логику, например, отправку данных с датчика
        # time.sleep(5)
        # client.push_dps({'1': True}) # Пример отправки статуса датчика 1
        time.sleep(1)
except KeyboardInterrupt:
    # Корректное завершение при нажатии Ctrl+C
    client.disconnect()
