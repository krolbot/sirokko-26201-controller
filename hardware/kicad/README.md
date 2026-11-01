# KiCad — единственный действующий проект

**DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.**

Открывайте `Sirokko26201_control_motor_candidate.kicad_pro` в KiCad 10.0.8. В этом каталоге находятся единственные действующие `.kicad_pro`, корневая схема и PCB, а также подключённые дочерние листы, проектные символы, посадочные места и локальные 3D-модели.

Контрольные суммы:

- `.kicad_pro`: `11029d86464484cea63d73aa802bad95c1a6805aee11e841fc78322e9d2bc0d6`;
- root SCH: `6c62c1d9fc13e621cfff8c6be2f9548082b5b4e7aaf046d7d7f454c1077fc76a`;
- PCB: `6b468c5fa23f7dce844ba41a267a18a37fbd6e21685c135afdaadc7fb7f77b5e`;
- `fp-lib-table`: `8e9cf2caf9090ad7d5e78625f981dc0fe8f22895f48515eb55261a27f1c247ac`.

PCB имеет контур 305 × 170 мм. KiCad 10.0.8 заново залил 19 зон; DRC=0, ERC=0, разрывы=0, `validate_board=ok`. Нативная проверка до и после заливки сохранила 70 посадочных мест, 287 площадок, 617 сегментов, 139 переходов, 27 зон и 1600 физических пар площадок.

Проектные зависимости разрешаются через `${KIPRJMOD}` в `fp-lib-table` и `sym-lib-table`; стандартные модели — через `${KICAD10_3DMODEL_DIR}`. Имена отдельных дочерних листов и библиотек содержат `DRAFT`, потому что инженерная приёмка не завершена; это не параллельные проекты.

Действующая карта соединений: [ELECTRICAL_INTERFACE_RU.md](ELECTRICAL_INTERFACE_RU.md). Закупочный состав остаётся черновым: [BOM_PURCHASE_NOTES_RU.md](../../docs/BOM_PURCHASE_NOTES_RU.md). Производственного архива в текущем дереве нет. Прежние состояния доступны только в истории Git.
