import os
from tuya_connector import TuyaOpenAPI
import time

# --- ТВОИ КОНФИГИ ---
ACCESS_ID = os.environ["TUYA_ACCESS_ID"]
ACCESS_KEY = os.environ["TUYA_ACCESS_KEY"]
ENDPOINT = "https://openapi.tuyaus.com" # Или твой регион (us/cn)
DEVICE_ID = os.environ["TUYA_DEVICE_ID"]

def control_device(status: bool):
    openapi = TuyaOpenAPI(ENDPOINT, ACCESS_ID, ACCESS_KEY)
    openapi.connect()

    # Для TuyaLink мы отправляем "свойства" в Shadow (тень) устройства
    # Используем идентификатор 'a', который мы выяснили ранее
    payload = {
        "properties": {
            "whore": str(status).lower() # Иногда нужно "true"/"false" строкой
        }
    }

    # ВАЖНО: URL метода для TuyaLink Properties
    url = f"/v2.0/cloud/thing/{DEVICE_ID}/shadow/properties/issue"

    response = openapi.post(url, payload)

    if response.get("success"):
        print(f"Успешно отправлено свойство: {status}")
    else:
        print(f"Ошибка API: {response}")

# --- ЗАПУСК ---
if __name__ == "__main__":
    # Включаем
    control_device(True)
    time.sleep(2)
    # Выключаем
    control_device(False)
