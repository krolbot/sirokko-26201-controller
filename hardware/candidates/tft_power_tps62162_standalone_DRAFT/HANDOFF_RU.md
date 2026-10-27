# Sirokko TPS62162 — назначение 8 Footprint

Статус: инженерный CAD-шаг выполнен и проверен. DRAFT / NOT FOR ORDER / NO FIRE. Это отдельный лист, не интегрированный в rootSCH/PCB; production_release=false, HOLD.

## Результат

В активном Lab-листе `/projects/exports/sirokko_final_panel_candidate_v2/tft_power_tps62162_review_DRAFT.kicad_sch` штатной операцией KiCad последовательно назначены ровно восемь полей Footprint:

- C301, C302 — `Sirokko_TFT_Passives:C_Murata_GRM21B_2012_Reflow`
- C303 — `Sirokko_TFT_Passives:C_Murata_GRM188_1608_Reflow`
- C304 — `Sirokko_TFT_Passives:C_Murata_GRM32E_3225_Reflow`
- L301 — `Sirokko_TFT_Passives:L_Wuerth_744025003_2828_Reflow`
- R301, R302 — `Sirokko_TFT_Passives:R_Yageo_RC0603_1608_Reflow`
- U301 — `Package_SON:Texas_DSG0008A_WSON-8-1EP_2x2mm_P0.5mm_EP0.9x1.6mm_ThermalVias`

Исходная SCH SHA256: `651c8f13e90f7c0329a827f7a82aefc3a21aa0208c13665af1134b7c840bb42f`.
Конечная активная SCH SHA256: `b65fb91e768ad472835623af1688074119e5ac5be67cb975eaba2c9c839b45a2`.
Побайтовый diff SCH содержит только восемь замен пустого значения Footprint на заданное: 8 добавленных и 8 удалённых строк, без иных атомов.

## Проверки

KiCad 10.0.7 заново открыл лист и выполнил native export XML/PDF/SVG и ERC. Для пользовательской библиотеки в комплект добавлен отдельный минимальный `.kicad_pro` и локальный `fp-lib-table`, указывающий на замороженный `passives_v3`; основную root-конфигурацию не меняли. Первый ERC свободного листа показал семь предупреждений отсутствующей библиотеки; их не подавляли. Повтор в самодостаточном проектном контексте: 0 нарушений, прежние ignored_checks `single_global_label`, `four_way_junction`, `simulation_model_issue`, `footprint_filter`.

Экспорт XML подтверждает 8 назначенных экземпляров и неизменную полную карту 6 сетей / 23 вывода; U301.9 (EP) остаётся на `/GND`. XML-diff после служебных `source/date/Sheetname/Sheetfile` содержит только восемь Footprint; libparts/libraries/nets и остальные поля не изменены. C303 остаётся `GRM188R71E104KA01D`. У R301/R302 сохранены `RC0603FR-07100KL`, Yageo, ChipDip9000632023, Datasheet и Supplier_URL. Оба PWR_FLAG сохранены.

Источник passives_v3: архив SHA256 `f9694ad9d8b7fa449d662da2aff55f4f352c30cc541b128a670abfe743609a7a`; пять `.kicad_mod` в комплекте совпадают с ним. Системный U301-файл на Lab перед использованием проверен: SHA256 `1766b4fbe43f04028b62b2c0b616bb23b7a93b1f5d68acb4d5f11ae00c115e43`; библиотека не переписывалась. Геометрия U301 остаётся принятой только для этого шага: EP 0,9×1,6 мм, два drill 0,2 мм при Y=±0,55; сигнальные pad 0,6×0,25 мм, зазор до EP 0,2 мм, paste 81,225%. Это не thermal/assembly PASS.

Конечный PDF преобразован в PNG 300 dpi и осмотрен целиком: лист A4 landscape не обрезан, блокирующих наложений нет, зазор +5V_CARRIER/PWR_FLAG сохранён, U301 и три GND читаемы, рамка и штамп целы. Пустые Title/Date/Rev допустимы для текущего DRAFT и не означают production release.

## Комплект

- редактируемые SCH + вспомогательные PRO/fp-lib-table;
- замороженный passives_v3;
- XML, PDF, SVG, PNG 300 dpi;
- ERC JSON;
- NET_MAP.json;
- VERIFICATION.json;
- U301 review evidence;
- SHA256SUMS.

Не выполнены и не заявляются: rootSCH/rootPCB, автоматический импорт, трассировка, production BOM, firmware, Ceff/DC-bias, L(I,T), общий бюджет 5 В, тепловой режим, пуск/переходные процессы, SPI backfeed, трафарет/маска/закрытие отверстий, физические испытания и production release.
