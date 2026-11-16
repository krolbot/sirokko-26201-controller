# Прошивка ESP32-S3

[Запись готовой прошивки и настройка](../docs/user-guide.md#первоначальная-настройка).

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

Параметры сборки — в `sdkconfig.defaults`; локальный `sdkconfig` имеет приоритет.

Образы прошивки появляются в `firmware/build/`. Запись по USB на вынутый из платы DevKit:

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

Для генерации шрифтов и иконок нужны Pillow/FreeType и `rsvg-convert`:

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

Вместо `--key` можно задать переменную `SIROKKO_OTA_SIGNING_KEY`.

При смене ключа соответствующий открытый ключ включают в прошивку и записывают её через USB.

## Сторонние материалы

- [js-sha256 0.11.1](https://github.com/emn178/js-sha256/tree/v0.11.1), Chen, Yi-Cyuan — [MIT](web_ui/licenses/js-sha256-MIT.txt). Расчёт хеша OTA при недоступном Web Crypto.
- Roboto — [SIL Open Font License](assets/OFL-Roboto.txt).
- [unslop](https://github.com/heydamir/unslop), Damir Sokolovsky — CC BY 4.0, оформление интерфейса.
- Лицензии зависимостей веб-интерфейса — [THIRD_PARTY_NOTICES.txt](web_ui/THIRD_PARTY_NOTICES.txt).
