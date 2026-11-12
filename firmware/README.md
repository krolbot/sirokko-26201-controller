# Прошивка Sirokko 262.01

Версия сборки — `1.0.0`; цель — ESP32-S3 с ESP-IDF 5.2.3. Карта разделов, GPIO, NVS и ключ доверия `main/ota_public_key.pem` являются частью прошивки.

## Воспроизводимая сборка

Из корня репозитория нужны Python 3, Node.js 26 и ESP-IDF 5.2.3.

```sh
npm ci --prefix firmware/web_ui
npm run build --prefix firmware/web_ui
python3 firmware/tools/embed_web_ui.py
python3 firmware/tools/embed_web_ui.py --check

cd firmware
idf.py set-target esp32s3
idf.py build
```

`web_ui_data.inc` — проверяемый встроенный gzip-образ production UI. Шрифты и иконки панели генерируются только при изменении исходных assets:

```sh
python3 firmware/tools/generate_panel_fonts.py --check
python3 firmware/tools/generate_panel_icons.py --check
```

Для этих двух генераторов нужны Pillow/FreeType и `rsvg-convert`; в самом устройстве они не нужны. CI собирает UI, сверяет встроенные байты, собирает ESP32-S3 и прикладывает полный набор для записи вместе с `flasher_args.json`, `flash_args` и `SHA256SUMS`.

## Запись и подключение

Первую запись выполняйте через USB на вынутом из платы DevKit, без подключённых исполнительных нагрузок. После сборки из каталога `firmware`:

```sh
idf.py -p /dev/ttyACM0 flash monitor
```

Замените `/dev/ttyACM0` фактическим портом. Для полного готового архива CI распакуйте все файлы с сохранением подкаталогов; из каталога архива в окружении ESP-IDF:

```sh
sha256sum -c SHA256SUMS
python -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash @flash_args
```

Эта запись не подготавливает исходные пределы NVS автоматически. Без них запуск остаётся заблокированным. После установки DevKit и включения платы откройте на местной панели пункт **Wi-Fi**: там показаны имя сети и индивидуальный пароль. Подключитесь к этой сети и откройте `http://192.168.4.1`.

## Исходные пределы и калибровка

Для конкретной сборки сначала подготовьте квалифицированную запись `SafetyEnvelope`; числовых значений по умолчанию нет. Пока валидная запись отсутствует, калибровка и подача топлива заблокированы.

`prepare_factory_envelope.py` проверяет полный вход с `qualification_reference` и всеми полями `SafetyEnvelope`, кодирует запись для ABI цели и ничего не записывает в устройство:

```sh
python3 firmware/tools/prepare_factory_envelope.py \
  --input /protected/qualified-envelope.json \
  --output-dir /protected/sirokko-envelope

cd /protected/sirokko-envelope
python3 "$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" \
  generate envelope.csv nvs.bin 0x6000
```

`0x6000` — размер раздела `nvs` из `partitions.csv`. Не перезаписывайте уже заполненный NVS: в нём хранятся настройки и учётные данные.

## Подписанный OTA

После target-сборки подпишите ровно её application image закрытым RSA-3072 ключом владельца. Инструмент требует явный `--key` либо `SIROKKO_OTA_SIGNING_KEY`, принимает только owner-only файл и не выводит ключ.

```sh
export SIROKKO_OTA_SIGNING_KEY=/protected/sirokko-ota-rsa3072.pem
python3 firmware/tools/sign_ota.py \
  --input firmware/build/sirokko_26201_controller.bin \
  --output sirokko-1.0.0.sota
```

В устройстве остаётся только `main/ota_public_key.pem`; OTA проверяет подпись, проект, цель ESP32-S3 и размер до выбора загрузочного раздела.
