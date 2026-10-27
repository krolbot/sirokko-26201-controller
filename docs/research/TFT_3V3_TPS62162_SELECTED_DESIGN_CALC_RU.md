# TFT_3V3 — выбранный узел TPS62162DSGR: расчёт DRAFT

**DRAFT / NOT FOR ORDER / NO FIRE. `standalone_not_integrated`; `production_release=false`.**

**Текущий статус:** STANDALONE SCHEMATIC CREATED / NOT INTEGRATED / NOT FOR ORDER. Создан отдельный проверочный лист, но не изменены основная SCH/PCB и производственная BOM. Прежний scratch-отчёт — исторический; актуальный источник — этот документ. Условия расчёта: VIN=4.75…5.25 V, VOUT=3.3 V, IOUT=0…0.400 A, Ta=60°C. Они не являются измеренными параметрами TFT и не закрывают полный бюджет carrier 5 V.

## Выбранный состав

| Ref | MPN | Значение/назначение | Основание |
|---|---|---|---|
| U_TFT3V3 | TPS62162DSGR | fixed 3.3 V, 1 A, WSON-8 2×2 с EP | TI SLVSAM2E Rev.E; ChipDip 8025077974 |
| L_TFT3V3 | Würth 744025003 | 3.3 µH ±20%, DCR max 101 mΩ@20°C; IR=1.5 A typ при ΔT40K, ISAT=1.8 A typ при потере L до35%; корпус2828 | TI Table 3; ChipDip 8966399095 |
| C301/C302, по 1шт | GRM21BR61E106KA73L | суммарно20µF номинально,25V,X5R,0805 | ChipDip9000122618:8867шт,17руб/шт,кратность5,5дней; точные паспорта прочитаны |
| C_IN2 | GRM188R71E104KA01D | 0.1 µF, 25 V, X7R, 0603 | ChipDip 9000121489 |
| C304 | GRM32ER61C476KE15L | 47µF,16V,X5R,1210 | ChipDip9000070696:14888шт,34руб/шт,кратность5,5дней; точные паспорта прочитаны |
| R_EN | 100 kΩ, 1%, 0603 | +5V_CARRIER→EN | EN high; внутренний pull-down ≈400k существует, внешний pull-down не ставить |
| R_PG | 100 kΩ, 1%, 0603 | TFT_3V3→PG | open-drain PG, только сигнал готовности, не источник питания |

Прежние строки GRM21BR71E106KA73L/GRM32ER71E476KE15L не применять: их исполнение/поставки не подтверждены; они не объявлены несуществующими. После проверки карточек, конкретных Murata Reference Sheet и Product Search Data Sheet выбраны реальные X5R детали выше для изолированного листа. Это осознанное изменение диэлектрика и выходного номинального напряжения, не исправление одной буквы в прежнем MPN. X5R допустим по TI§9.2.2.5.1; собственная температура конденсаторов должна быть не выше85°C. В карточках ошибочное поле допуска0.1: по паспортам ±10%, а не ±0.1%. Заказ не выполнялся. Отношения номинального напряжения к рассмотренной шине:25/5.25=4.762 длявхода и16/3.432=4.662 длявыхода, более1.3; импульсный запас и ток/нагрев MLCC отдельно не подтверждены. Типовые DCbias-графики открыты и визуально проверены; это не гарантированные минимумы Ceff.

## Соединения

В отдельном листе: U301=U_TFT3V3,L301=L_TFT3V3,R301=R_EN,R302=R_PG,C303=C_IN2. C301 иC302 параллельно на входе, C304 на выходе. Все локальные GND одной сетью; геометрия единой точки на PCB ещё не существует. 1 PGND → GND_PG; 2 VIN → +5V_CARRIER через C_IN1/C_IN2 на PGND; 3 EN ← R_EN от +5V_CARRIER; 4 AGND и EP → GND_AGND/GND plane в одной точке у EP; 5 FB → AGND (наш выбранный вариант fixed version; TI рекомендует, а не требует это соединение); 6 VOS → positive C_OUT/TFT_3V3 Kelvin; 7 SW → L → TFT_3V3; 8 PG → R_PG→TFT_3V3, test point/MCU input only. C_OUT negative → PGND near U. No diode is required in the synchronous buck power path.

## Расчёт

TI Table 2 подтверждает nominal LC = 3.3 µH / 47 µF and explicitly assumes typically ±20% combined L/C variation. With Lmin=2.64 µH and fSW=2.25 MHz typ:

- At VIN=4.75: ΔIL=169.591 mA, ILpk=484.795 mA, ILrms=402.985 mA, CIN ripple current:184.208mArms ignores inductor ripple;188.673mArms including triangular inductor ripple.
- At VIN=5.25: ΔIL=206.349 mA, ILpk=503.175 mA, ILrms=404.411 mA, CIN ripple current:193.275mArms ignores inductor ripple;198.961mArms including triangular inductor ripple.
- 30%-reserve target relative to the calculated peak is 0.654127 A. NO guaranteed-current PASS: 1.5 A is a typical thermal IR rating, not the saturation limit. Manufacturer ISAT is1.8 A typical at up to35% inductance reduction; no guaranteed L(I,T) minimum established. Frequency2.25MHz is typical, so0.503175A is a screening point, not a proven worst-case peak. Startup/short-circuit behavior and current-limit overshoot remain unqualified.
- Inductor copper loss screening at DCRmax 101 mΩ and ILrms=404.411 mA: 16.518 mW at the stated DCR condition; temperature increase of DCR not calculated.
- If C_OUT effective minimum is at least 37.6 µF (47 µF × 0.8 assumed envelope), capacitive PWM ripple model = 0.305 mVpp plus ΔIL×ESR. ESR and PSM ripple are not bounded by this calculation.
- fLC=12.780 kHz nominal; 15.974 kHz in the stated L/C low model. These are within the TI proven nominal 3.3µH/47µF table cell, not a substitute for exact component DC-bias data.
- C_IN ripple voltage model with conditional totalCeff=8µF (now two10µF capacitors; this8µF is NOT a proven minimum and not a20%-loss claim) =5.188mVpp at5.25VIN, using approximate ΔVIN=IOUT·D·(1−D)/(f·CIN), plus ESR/ESL and source impedance not included; the0.1µF is only HF decoupling. The updated30% current-rating target is258.649mArms. Formula:CIN_RMS=sqrt(IOUT²·D·(1−D)+D·ΔIL²/12), D=VOUT/VIN, ideal CCM, triangular ripple. PSM/startup not covered. MLCC ripple-current rating and Ceff still require exact proof.
- VOUT initial device-only intervals: PWM 3.201…3.399 V; PSM3.1845…3.432V is the table value explicitly conditioned onCOUT22µF, not a guaranteed interval transferred to the47µF candidate. PWM and PSM values are alternative modes, not additive. Datasheet gives only typ line/load regulation: 0.660 mV across 0…0.4 A and 0.330 mV across 4.75…5.25 V; these are not guaranteed extrema and are not added to initial accuracy.
- POUT=1.32 W. Heat cannot be guaranteed at 5 V because RDS(on) maxima apply at VIN≥6 V, not 5 V. With RθJA=61.8°C/W only as JEDEC reference, allowable total IC loss at Ta60/Tj125 is 1.052 W; with an independent 25% loss margin it is 0.841 W. No thermal PASS without board copper/EP/vias and verified loss.
- 5V input current illustrative only: at η=80%, 0.347368 A at VIN=4.75 V. It must be added to all other carrier loads by the parent budget; the separate 1 A TPS rating does not qualify carrier 5 V.

## Sources

- TI, `SLVSAM2E`, Rev.E, printed pp. 6 (electrical characteristics), 14–16 (LC/inductor/capacitor selection), 23 (3.3V example), 26–27 (layout/thermal); manufacturer PDF: https://www.ti.com/lit/ds/symlink/tps62160.pdf .
- ChipDip TPS62162DSGR 8025077974: https://www.chipdip.ru/product/tps62162dsgr-texas-instruments-8025077974 ; linked PDF https://static.chipdip.ru/lib/796/DOC059796460.pdf.
- Würth exact-part manufacturer datasheet, revision002.003 dated2020-06-09, p1: https://www.we-online.com/components/products/datasheet/744025003.pdf . IR and ISAT are explicitly typical and are different criteria.
- ChipDip Würth 744025003 / 8966399095: https://www.chipdip.ru/product/744025003 .
- ChipDip GRM188R71E104KA01D / 9000121489: https://www.chipdip.ru/product/grm188r71e104ka01d .

## Новый нативный результат

- Создан отдельный [`tft_power_tps62162_review_DRAFT.kicad_sch`](../../hardware/candidates/tft_power_tps62162_standalone_DRAFT/tft_power_tps62162_review_DRAFT.kicad_sch):8электрических компонентов+2PWR_FLAG,6сетей,23проводных отвода,23метки; основныеPCB/SCHне менялись. Флаги объявляют только внешний проверочный источник5В/землю, не доказанную интеграцию LM5013.
- TPS62162DSG — наследуемый символ; MCP корректно отказал без записи. Вместо ремонта сервера использована штатная базовая геометрия TPS62170DSG с ValueTPS62162DSGR, исправленными Datasheet/Description. Все9выводов независимо сверены сTI§6p4, затем прочитаны обратным запросом6сетей. Это не замена ИС наTPS62170.
- ERC KiCad10.0.7:0violations, изолированный лист не входит вroot-проект. Это не системный ERC или проверка работоспособности. ЭкспортированыXML13294байта иPDF67402байта в отдельный каталог экспорта; PDF ещё не прошёл визуальную приёмку координатора.
- Входная закупочная карточка: https://www.chipdip.ru/product/10mkf-x5r-25v-10-0805-grm21br61e106ka73l-kondensator-murata-9000122618 ; ReferenceSheet2016: https://static.chipdip.ru/lib/630/DOC011630562.pdf ; типовые характеристики2020: https://static.chipdip.ru/lib/971/DOC031971891.pdf .
- Выходная карточка: https://www.chipdip.ru/product/47mkf-x5r-16v-10-1210-grm32er61c476ke15l-kondensator-murata-9000070696 ; ReferenceSheetNov4,2021: https://static.chipdip.ru/lib/039/DOC028039487.pdf ; типовые характеристики2021: https://static.chipdip.ru/lib/973/DOC031973436.pdf .
- У типовой выходной кривой около4В потеря порядка30%; автоматически считать47µF×0.8 нельзя. Числа37.6µF/0.305mV выше — лишь условная старая модель. Для чувствительности, еслиCeff20µF:пульсация PWM≈0.573mVpp+ESR/ESL иfLC≈21.903kHz приL2.64µH/fSW2.25MHztyp;20µF не объявлены гарантией выбранной детали.
- Дополнительно прочитан TI SLVA463A July2015,§1/table1: в испытаниях эффективная ёмкость принята половиной номинальной, приVIN12В/VOUT3.3В/IOUT1А. Это другая расчётно-испытательная основа, не разрешение считать любую потерюCeff универсально допустимой. https://www.ti.com/lit/an/slva463/slva463.pdf .

## Remaining acceptance

Before CAD release: exact Murata Ceff-vs-bias/tolerance/temperature proof, footprint + EP stencil/thermal-via implementation, 5V shared budget, and schematic review of EN/PG ownership. Before production: full thermal layout calculation and bench tests of startup/inrush, ripple/overshoot, PSM/PWM noise near thermocouples, load steps and cable-end VOUT. No physical test has been performed.

## Примечание издателя: повторная проверка отдельного листа

Исходный расчёт выше сохранён без изменения MPN, чисел и ограничений; удалены только частные рабочие пути. [Комплект отдельного листа](../../hardware/candidates/tft_power_tps62162_standalone_DRAFT/README_RU.md) содержит исходную SCH/PDF, публичный XML, PNG, полный ERC и карту сетей. Повторный штатный экспорт KiCad 10.0.7 дал PDF, отличающийся только метаданным CreationDate (растр PNG побайтно совпал), и ту же карту всех шести сетей; ERC — 0 нарушений при четырёх отключённых проверках, перечисленных в отчёте. PDF визуально осмотрен: рамка целая, но подписи U301/L301 и входной группы перекрываются. Оформление не принято; схема не редактировалась. Это не интеграция в root, не PCB, не производственная BOM и не системная квалификация; общий HOLD сохранён.
