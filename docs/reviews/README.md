# Инженерные обзоры для metadata_358262f0 и pending BOM

**DRAFT / NOT FOR ORDER / NO FIRE.** `production_release=false`.

Это индекс собственных расчётов и обоснований к замороженному [native-срезу PCB/SCH](../../hardware/candidates/metadata_358262f0/README.md) и следующему единому [кандидату дополнения BOM на 46 позиций](../pending_bom_46refs/README_RU.md). Прежний [кандидат на 32 позиции](../pending_bom_32refs/README_RU.md) сохранён как неизменяемый исторический снимок и не должен использоваться параллельно с новым. Документы не являются полной электрической, тепловой, DFM- или производственной квалификацией. Сторонние паспорта и изображения в репозиторий не включены; в документах сохранены только ссылки на источники.

## Расчёты узлов

- [Тепловой бюджет LM317HV](lm317hv/LM317HV_THERMAL_BUDGET_CURRENT_RU.md) и [машинные данные](lm317hv/LM317HV_THERMAL_BUDGET_CURRENT_DRAFT.json).
- [Проверка bootstrap LTC7001 Rev.G](ltc7001_bootstrap/LTC7001_BOOTSTRAP_REVG_CURRENT_SCREEN_RU.md) и [машинные данные](ltc7001_bootstrap/LTC7001_BOOTSTRAP_REVG_CURRENT_SCREEN_DRAFT.json).
- [Адресный аудит входа питания](../research/INPUT_POWER_INTERFACE_AUDIT_RU.md): J13 не является физически отделённым логическим входом; полный входной узел и токовая квалификация остаются незакрытыми.
- [Исправленный расчёт F1](../../hardware/candidates/input-protection/INPUT_PROTECTION_F1_CLOSURE.md) и [машинные данные](../../hardware/candidates/input-protection/F1_0997030_CONTINUOUS_RERATING_SCREEN.json): 0997030.WXN не проходит предварительный непрерывный расчёт при 60 и 85 °C для условных 480 W / 21,6 V.

## Подборы для единого кандидата BOM

- [Murata 100 нФ 0603](mlcc_100n_0603/SELECTION_RU.md): адресный список, статическая проверка и аудит геометрии посадочного места.
- [YAGEO 10 кОм 0603](rc0603_10k/SELECTION_RU.md): адресный список и статическая проверка.
- [Три резистора моторного узла](motor_3_resistors/SELECTION_RU.md): адресный список и статическая проверка.

Следующий единый сводный кандидат BOM находится в `docs/pending_bom_46refs/PROPOSED_BOM_NOT_CANONICAL.csv`: 128 строк, 105 заполненных MPN, 23 пустых, 46 изменяемых reference. Он сохраняет прежние 32 изменения и добавляет ровно R2…R15; это не BOM активной PCB. Файлы обзоров не создают конкурирующую каноническую BOM и не изменяют native CAD.
