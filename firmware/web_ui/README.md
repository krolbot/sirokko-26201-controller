# Веб-интерфейс Sirokko

Исходники Svelte собираются в один автономный HTML-документ с локальными шрифтами, лицензиями и OTA SHA-256 fallback; CDN и runtime Node.js на устройстве не нужны.

Из корня репозитория:

```sh
npm ci --prefix firmware/web_ui
npm run build --prefix firmware/web_ui
python3 firmware/tools/embed_web_ui.py
python3 firmware/tools/embed_web_ui.py --check
```

`npm run build` формирует `dist/index.html`, обновляет `THIRD_PARTY_NOTICES.txt` и проверяет самодостаточность страницы. `embed_web_ui.py` сжимает именно этот документ и записывает `../main/web_ui_data.inc`; `--check` подтверждает совпадение встроенных байтов с production-сборкой.
