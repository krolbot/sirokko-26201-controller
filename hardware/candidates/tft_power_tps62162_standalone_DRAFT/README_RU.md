# TPS62162: отдельный лист с восемью назначенными Footprint

DRAFT / NOT FOR ORDER / NO FIRE. `standalone_not_integrated`; `production_release=false`; HOLD.

Это проверенный отдельный CAD-снимок, не интеграция в rootSCH/PCB и не производственный выпуск. Основная схема, PCB, производственная BOM, прошивка и остальные кандидаты этой публикацией не меняются. Заказ, изготовление, подключение нагрузок и огневые испытания не разрешены.

## Источник и состав

Снимок инженера опубликован побайтно из `SIROKKO_TPS62162_FOOTPRINTS_ASSIGNED_DRAFT.zip`, SHA256 `000a30def8408b9868489c1c399972d26f2d4b1d62cd113173fa179b23852065`. Исходная база — commit `d8a79410a11bbbaa3a8fa6aa434f7c24d8d8b97d`; относительно неё SCH содержит только восемь замен значения Footprint, без иных изменений. Текущая SCH SHA256 `b65fb91e768ad472835623af1688074119e5ac5be67cb975eaba2c9c839b45a2`.

- [SCH](tft_power_tps62162_review_DRAFT.kicad_sch), [XML](tft_power_tps62162_review_DRAFT.xml), [PDF](tft_power_tps62162_review_DRAFT.pdf), [SVG](tft_power_tps62162_review_DRAFT.svg), [PNG 300 dpi](tft_power_tps62162_review_DRAFT.png), 3508×2481 px.
- [ERC](tft_power_tps62162_review_DRAFT-erc.json), [полная карта сетей](NET_MAP.json), [проверки инженера](VERIFICATION.json), [передача результата](HANDOFF_RU.md), [исходные SHA256](SHA256SUMS).
- Вспомогательный [минимальный PRO](tft_power_tps62162_review_DRAFT.kicad_pro) и [fp-lib-table](fp-lib-table) задают контекст локальной библиотеки. Они не заменяют основной проект.
- [Замороженный passives_v3](passives_v3/SHA256SUMS): все 22 файла, включая пять `.kicad_mod`, сохранены без изменения; [отдельная проверка геометрии U301](u301_review/TFT_U301_FOOTPRINT_REVIEW_RU.md).
- [Публичный манифест](PUBLIC_MANIFEST.json) и `python3 VERIFY.py` проверяют точный состав, размеры/SHA256, карту соединений, значения, назначения Footprint и сохранённый ERC. Это проверка опубликованных данных, не новый запуск KiCad.

Устаревшие отчёты предыдущего снимка с пустыми Footprint сохранены в [истории базового коммита](https://github.com/krolbot/sirokko-26201-controller/tree/d8a79410a11bbbaa3a8fa6aa434f7c24d8d8b97d/hardware/candidates/tft_power_tps62162_standalone_DRAFT), а не выданы за текущие. [Авторский расчёт](../../../docs/research/TFT_3V3_TPS62162_SELECTED_DESIGN_CALC_RU.md) этой публикацией не меняется. Сторонние PDF не включены.

## Назначения и проверки

| Ref | Footprint |
|---|---|
| C301, C302 | `Sirokko_TFT_Passives:C_Murata_GRM21B_2012_Reflow` |
| C303 | `Sirokko_TFT_Passives:C_Murata_GRM188_1608_Reflow` |
| C304 | `Sirokko_TFT_Passives:C_Murata_GRM32E_3225_Reflow` |
| L301 | `Sirokko_TFT_Passives:L_Wuerth_744025003_2828_Reflow` |
| R301, R302 | `Sirokko_TFT_Passives:R_Yageo_RC0603_1608_Reflow` |
| U301 | `Package_SON:Texas_DSG0008A_WSON-8-1EP_2x2mm_P0.5mm_EP0.9x1.6mm_ThermalVias` |

Сохранены шесть сетей / 23 электрических вывода, EP9→`/GND`, оба PWR_FLAG, все значения, UUID, якоря и провода. C303 остаётся `GRM188R71E104KA01D`; R301/R302 — `RC0603FR-07100KL`, с прежними скрытыми закупочными свойствами. Начальный `/` в XML — штатная часть имени сети.

U301 использует геометрию символа TPS62170DSG, но выбранный Value/MPN — TPS62162DSGR; библиотечное имя не является заменой выбранной ИС. Системная посадка U301 не включена в архив: требуется штатная `Package_SON` KiCad с проверенной версией; её исходный SHA записан в VERIFICATION.json. Локальная библиотека пассивных компонентов включена полностью.

В сохранённом отчёте KiCad 10.0.7 — 0 нарушений ERC при прежних четырёх ignored_checks: `single_global_label`, `four_way_junction`, `simulation_model_issue`, `footprint_filter`. Это не строгий ERC со всеми проверками. Инженер повторно экспортировал XML/PDF/SVG и выполнил ERC в самодостаточном проектном контексте. Издатель проверил байты и данные этих результатов, но не заявляет нового запуска KiCad. Конечный PNG осмотрен: рамка цела, подписи и три GND читаемы, зазор +5V_CARRIER/PWR_FLAG сохранён.

## Ограничения

Назначение посадок не является приёмкой всей платы или разрешением автоматического импорта. Для U301 сохранены EP 0,9×1,6 мм, два отверстия 0,2 мм при Y=±0,55, сигнальные площадки 0,6×0,25 мм и paste 81,225%; отличия от рекомендации TI описаны в отдельном отчёте. Это не thermal/assembly PASS.

Открыты Ceff/DC-bias, L(I,T), общий бюджет 5 В, тепло, пуск/переходные процессы, SPI backfeed, трафарет/маска/закрытие отверстий и физические испытания. Ноль ERC, геометрия посадок и назначенные Footprint не доказывают электрическую работоспособность. HOLD и запрет production release сохранены.
