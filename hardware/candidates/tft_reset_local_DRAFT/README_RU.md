# Локальный сброс TFT — отдельный схемный кандидат

DRAFT / NOT FOR ORDER / NO FIRE. NOT INTEGRATED; HOLD; `production_release=false`.

TPS3808G33DBVR контролирует TFT_3V3; R303=100 кОм, C305=100 нФ. MR/CT намеренно не подключены. Связей с ESP32 EN, safety RESET и GPIO нет. Это не завершение аппаратного reset-контракта и не разрешение заменить R23. Основные SCH/PCB/BOM и прошивка не изменены.

- [Инженерный отчёт и ограничения](../../../docs/research/TFT_RESET_LOCAL_SUPERVISOR_REVIEW_RU.md).
- [Сопряжение Waveshare 18366](../../../docs/research/WAVESHARE_18366_INTEGRATION_RU.md).
- [Исходная SCH](sirokko_tft_reset_local_DRAFT.kicad_sch), [собственный PDF](sirokko_tft_reset_local_DRAFT.pdf), [PNG](sirokko_tft_reset_local_DRAFT.png).
- [Native XML](sirokko_tft_reset_local_DRAFT.xml), [полный ERC](sirokko_tft_reset_local_DRAFT-erc.json), [авторская проверка](VERIFICATION.json).

В авторском KiCad 10.0.7 выполнены повторное открытие и экспорт PDF/XML/ERC: 0 нарушений при четырёх `ignored_checks` (`single_global_label`, `four_way_junction`, `simulation_model_issue`, `footprint_filter`), не strict-all ERC. XML содержит 3 подключённые сети с 8 выводами и 2 отдельные intentional NC; карта совпадает с авторским JSON. Издатель проверил архив, хеши, эту карту, ERC-отчёт и изображение; штатные проверки KiCad повторно не запускал. Footprint экземпляров пустые.

Открыты пороги и переходные процессы TFT_3V3, кабель/нагрузка RST, обратное питание через SPI, повторная инициализация прошивки после потери питания только TFT, выбор обнаружения этого события и квалификация посадочных мест. Физических испытаний нет; заказ и огневые испытания запрещены.

## Происхождение и контрольные суммы

Исходный архив `SIROKKO_TFT_RESET_LOCAL_REVIEW_DRAFT.zip`: SHA256 `b57ae57611ca7b9b10efc5964d9c31c8f5f691644921149f0af4acc74799b143`, 9 файлов, CRC и 8 исходных SHA256 проверены. Шесть инженерных файлов в этом каталоге сохранены побайтно. Сторонние PDF, включая паспорт TI, не публикуются; в отчёте оставлены ссылки на источники.

[SOURCE_SHA256SUMS](SOURCE_SHA256SUMS) — неизменённый реестр исходного архива, не команда проверки опубликованного дерева: его два MD перенесены в `docs/research`, ссылки адаптированы, в существующем документе сопряжения заменён только блок RST с сохранением более новых дополнений main. [SHA256SUMS](SHA256SUMS) проверяет фактические опубликованные файлы, включая оба MD и исходный реестр. Запускать из этого каталога: `sha256sum -c SHA256SUMS`.
