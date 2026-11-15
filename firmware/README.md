# Прошивка ESP32-S3

Для записи готовой прошивки и первоначальной настройки используйте [руководство пользователя](../docs/user-guide.md#первоначальная-настройка).

## Инструменты

- ESP-IDF **5.2.3**, настроенный для **ESP32-S3**;
- Node.js **26** с npm;
- Python 3 из окружения ESP-IDF.

Версии компонентов ESP-IDF и веб-зависимостей закреплены в `dependencies.lock` и `web_ui/package-lock.json`.

## Сборка

Команды выполняются из корня репозитория в окружении ESP-IDF:

```sh
npm ci --prefix firmware/web_ui
npm run build --prefix firmware/web_ui
python3 firmware/tools/embed_web_ui.py
python3 firmware/tools/embed_web_ui.py --check
idf.py -C firmware set-target esp32s3
idf.py -C firmware build
```

Параметры сборки заданы в `sdkconfig.defaults`: HTTP-заголовки до 8192 байт, стек main 8192 байта, BSS в PSRAM. Локальный `sdkconfig` имеет приоритет над этими значениями.

Результат — `firmware/build/`. Для USB-записи своего образа на вынутый из основной платы DevKit:

```sh
idf.py -C firmware -p /dev/ttyACM0 flash monitor
```

В Windows порт обычно имеет вид `COM5`. GitHub Actions собирает прошивку и формирует комплект для USB-записи.

## Исходники

| Каталог / файл | Назначение |
|---|---|
| `components/control_core/` | Управление отопителем и регулирование |
| `main/board.hpp` | Аппаратные назначения |
| `main/app_main.cpp`, `main/sensors.cpp` | Работа ESP-IDF и датчиков |
| `main/panel*` | Экран и местное управление |
| `main/web*`, `web_ui/src/` | HTTP API и веб-интерфейс |
| `main/persistence.hpp`, `main/record_codec.hpp` | Хранение параметров |
| `partitions.csv`, `sdkconfig.defaults` | Разделы flash и конфигурация сборки |

Шрифты, иконки и встроенная веб-страница лежат в `main/*_data.inc`.

### Изменение веб-интерфейса

Исходники Svelte находятся в `web_ui/src/`. Сборка создаёт самодостаточный `web_ui/dist/index.html` и обновляет `web_ui/THIRD_PARTY_NOTICES.txt`. Скрипт `embed_web_ui.py` сжимает страницу в `main/web_ui_data.inc`; этот файл обновляется вместе с исходниками интерфейса.

`npm run preview --prefix firmware/web_ui` открывает локальный предпросмотр; управление устройством требует подключения к его API.

### Изменение шрифта и иконок

Готовые шрифты и иконки включены в исходники. Для их изменения нужны Pillow/FreeType и `rsvg-convert`:

```sh
python3 firmware/tools/generate_panel_fonts.py
python3 firmware/tools/generate_panel_icons.py
```

Результат генерации зависит от версий Pillow и FreeType.

## Подпись OTA

Веб-обновление принимает `.sota`, подписанный RSA-3072. Открытый ключ доверия включён в `main/ota_public_key.pem`; закрытый ключ в проект не входит.

Из корня репозитория, после сборки:

```sh
python3 firmware/tools/sign_ota.py \
  --input firmware/build/sirokko_26201_controller.bin \
  --output /path/to/sirokko.sota \
  --key /protected/sirokko-ota-rsa3072.pem
```

Путь к закрытому ключу задаётся параметром `--key` или переменной `SIROKKO_OTA_SIGNING_KEY`. Закрытый ключ хранится вне репозитория.

Для собственной системы обновлений нужен собственный ключ и соответствующий открытый ключ в прошивке. Сначала запишите такую прошивку через USB.
