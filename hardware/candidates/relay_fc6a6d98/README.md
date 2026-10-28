# Sirokko relay WIP fc6a6d98

**DRAFT / NOT FOR ORDER / NO FIRE / НЕ ДЛЯ ОГНЕВЫХ ИСПЫТАНИЙ.** `production_release=false`.

Это замороженная контрольная точка релейной интеграции, а не производственный выпуск. Откройте `project/Sirokko26201_control_motor_candidate.kicad_pro` в KiCad 10. Каталог `project/` содержит 77 файлов, корневую схему и все пять разрешимых дочерних листов, локальные библиотеки и таблицы. Native CAD перенесён без изменения байтов; в сопроводительных MD/JSON/XML нормализованы только машинно-локальные пути и пересчитаны два устаревших вложенных манифеста.

- PCB SHA-256: `fc6a6d98efadc539fd21af9af2b8381dd91618b5b57a4ce21c1aadf7d15004e3`.
- Корневая SCH SHA-256: `49a81ce6383e121ec857c6cdf1ce1889b7179d3dfc686141b4a27563874bbfbe`.
- U11 `DRV8803PWPR`, U12 `SN74HCS08D`, K1–K3 `DG85BM / DZ85AB`, C21/C22/C23.
- BOM: 140 строк, 121 MPN заполнен, 19 пустых.
- Независимая физическая проверка: 533→582 площадки, 5646→5648 пар, 0 потерянных, 2 добавленных (`C23.1-U12.14`, `C23.2-U12.7`), 0 межсетевых пар.
- ERC: 6 листов, 3 ошибки, 6 предупреждений.
- Переданная сводка DRC указывала 27/150/14 (violations/unconnected/parity), но вложенный `FINAL_DRC_PARITY.json` фактически содержит 27/164/11. Расхождение сохранено в [`PUBLIC_STATUS.json`](PUBLIC_STATUS.json); общий DRC не является PASS.

Проверка: [`review/REVIEW_RU.md`](review/REVIEW_RU.md), [`review/INDEPENDENT_NATIVE_PHYSICAL_PARTITIONS.json`](review/INDEPENDENT_NATIVE_PHYSICAL_PARTITIONS.json), [`verification/FINAL_DRC_PARITY.json`](verification/FINAL_DRC_PARITY.json), [`verification/FINAL_ERC.json`](verification/FINAL_ERC.json), [`verification/FINAL_BOM_DETAILED.csv`](verification/FINAL_BOM_DETAILED.csv).

Изображения: [F.Cu SVG](images/F_Cu.svg), [B.Cu mirrored SVG](images/B_Cu_mirrored.svg), [F.Cu PNG](images/F_Cu_DRAFT.png), [B.Cu mirrored PNG](images/B_Cu_mirrored_DRAFT.png). Это черновые обзоры, не Gerber/Drill и не производственный комплект.

Неверный helper `verify_connectivity_delta.py` и производный `CONNECTIVITY_DELTA.json` исключены: они группируют имена сетей, а не физическую медь. Общая компоновка, разводка, прошивка, производство и физические испытания не приняты. Проверка опубликованного каталога: `python VERIFY.py`.
