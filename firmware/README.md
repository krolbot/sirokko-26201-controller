# Разработка и сборка прошивки

Для записи готовой прошивки и первоначальной настройки используйте [руководство пользователя](../docs/user-guide.md#первоначальная-настройка).

## Инструменты

- ESP-IDF **5.2.3**, настроенный для **ESP32-S3**;
- Node.js **26** с npm;
- Python 3 из окружения ESP-IDF.

Версии компонентов ESP-IDF и веб-зависимостей закреплены в `dependencies.lock` и `web_ui/package-lock.json`.

## Собрать

Откройте терминал с активным ESP-IDF. Все команды ниже запускаются **из корня репозитория**:

```sh
npm ci --prefix firmware/web_ui
npm run build --prefix firmware/web_ui
python3 firmware/tools/embed_web_ui.py
python3 firmware/tools/embed_web_ui.py --check
idf.py -C firmware set-target esp32s3
idf.py -C firmware build
```

`sdkconfig.defaults` применяется при создании конфигурации. Если `firmware/sdkconfig` остался от прежней версии, обновите его: нужны HTTP-заголовки до 8192 байт, стек main 8192 байта и BSS в PSRAM.

Результат — `firmware/build/`. Для USB-записи своего образа на вынутый из основной платы DevKit:

```sh
idf.py -C firmware -p /dev/ttyACM0 flash monitor
```

Замените порт фактическим. GitHub Actions собирает ту же цель и сохраняет комплект с загрузчиком, таблицей разделов, образами и адресами записи.

## Где изменять

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

После правок повторите команды сборки выше. `npm run preview --prefix firmware/web_ui` открывает собранную страницу локально; для управления нужен API контроллера.

### Изменение шрифта и иконок

Генераторы находятся в `tools/`; нужны Pillow/FreeType и `rsvg-convert`. Для обычной сборки уже включённых данных эти инструменты не нужны. При изменении ресурсов запускайте генераторы из корня репозитория и проверяйте результат:

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

Путь к закрытому ключу задаётся параметром `--key` или переменной `SIROKKO_OTA_SIGNING_KEY`. Файл ключа должен быть доступен только владельцу.

Для собственной системы обновлений нужен собственный ключ и соответствующий открытый ключ в прошивке. Сначала запишите такую прошивку через USB. Не публикуйте закрытый ключ вместе с исходниками или выпуском.

## Тесты

Из корня репозитория; нужны g++, заголовки OpenSSL, Python 3 и Node.js:

```sh
python3 firmware/tools/test_host.py
node --test firmware/web_ui/tests/*.test.mjs
```

Тесты на компьютере проверяют переходы управления, обработку отказов, хранение настроек, обмен командами и подпись OTA. C++ запускается с ASan/UBSan.
