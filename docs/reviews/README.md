# Инженерные обзоры для metadata_358262f0 и pending BOM

**DRAFT / NOT FOR ORDER / NO FIRE.** `production_release=false`.

Это индекс собственных расчётов и обоснований к замороженному [native-срезу PCB/SCH](../../hardware/candidates/metadata_358262f0/README.md) и единому [кандидату дополнения BOM на 32 позиции](../pending_bom_32refs/README_RU.md). Документы не являются полной электрической, тепловой, DFM- или производственной квалификацией. Сторонние паспорта и изображения в репозиторий не включены; в документах сохранены только ссылки на источники.

## Расчёты узлов

- [Тепловой бюджет LM317HV](lm317hv/LM317HV_THERMAL_BUDGET_CURRENT_RU.md) и [машинные данные](lm317hv/LM317HV_THERMAL_BUDGET_CURRENT_DRAFT.json).
- [Проверка bootstrap LTC7001 Rev.G](ltc7001_bootstrap/LTC7001_BOOTSTRAP_REVG_CURRENT_SCREEN_RU.md) и [машинные данные](ltc7001_bootstrap/LTC7001_BOOTSTRAP_REVG_CURRENT_SCREEN_DRAFT.json).

## Подборы для единого кандидата BOM

- [Murata 100 нФ 0603](mlcc_100n_0603/SELECTION_RU.md): адресный список, статическая проверка и аудит геометрии посадочного места.
- [YAGEO 10 кОм 0603](rc0603_10k/SELECTION_RU.md): адресный список и статическая проверка.
- [Три резистора моторного узла](motor_3_resistors/SELECTION_RU.md): адресный список и статическая проверка.

Единственный сводный кандидат BOM находится в `docs/pending_bom_32refs/PROPOSED_BOM_NOT_CANONICAL.csv`: 128 строк, 91 заполненный MPN, 37 пустых, 32 изменяемых reference. Файлы обзоров не создают конкурирующую каноническую BOM и не изменяют native CAD.
