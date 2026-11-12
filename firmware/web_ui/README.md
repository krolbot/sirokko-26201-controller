# Веб-интерфейс Sirokko

Веб-интерфейс на Svelte. Сборка объединяет страницу, шрифты и библиотеку SHA-256 для OTA в один HTML-файл.

Из корня репозитория:

```sh
npm ci --prefix firmware/web_ui
npm run build --prefix firmware/web_ui
python3 firmware/tools/embed_web_ui.py
python3 firmware/tools/embed_web_ui.py --check
```

`npm run build` формирует `dist/index.html`, обновляет `THIRD_PARTY_NOTICES.txt` и проверяет самодостаточность страницы. `embed_web_ui.py` сжимает HTML и записывает `../main/web_ui_data.inc`; `--check` подтверждает совпадение встроенной страницы с результатом сборки.
