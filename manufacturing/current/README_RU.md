# Sirokko — единый текущий проверочный комплект

**DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.**
`production_release=false`; `physical_tests=false`. Программный DRAFT находится в [`../../firmware/`](../../firmware/) и не проходил физическую проверку.

## Источник

Производные файлы ниже проверены штатным KiCad 10.0.8 на Lab по канонической CAD-паре исходного коммита [`9405cabb3955a39df3351b2abdfdc21defeb73f5`](https://github.com/krolbot/sirokko-26201-controller/commit/9405cabb3955a39df3351b2abdfdc21defeb73f5). После metadata-only правок J1/J3 и F1 заново получены DRC, ERC, BOM, схемный netlist и полный PDF схемы; геометрически незатронутые Gerber, Excellon, CPL и виды сохранены:

- PCB SHA-256: `57f4b9437de4176f61e99d71ef923711f1368ba13d1de3b72c6fd13d82a4d09b`;
- корневая схема SHA-256: `0f62b97606d5220ebe1cb630b59fd2050777e6d9020e623b93a4c5518774f76e`;
- проект SHA-256: `11029d86464484cea63d73aa802bad95c1a6805aee11e841fc78322e9d2bc0d6`.

В CAD изменены только свойства F1: Value/MPN `0997015.WXN`, 15 А / 58 В DC, держатель Keystone 3568. До и после экспорта SHA совпали. В `hardware/kicad/` находится одна каноническая PCB/PRO, полная иерархия схемы, локальные таблицы библиотек, проектные символы, посадочные места и модели. Параллельных PCB и снимков в текущем комплекте нет.

## Проверки

- Root ERC: код 0, 0 нарушений — [`checks/erc.json`](checks/erc.json).
- Свежая нативная DRC на Lab сначала после `--refill-zones`, затем после повторного открытия: оба раза код 0, 0 нарушений, 0 несоединений, 0 расхождений PCB↔SCH, `ignored_checks=[]` — [`checks/drc.json`](checks/drc.json) и [`checks/native-reopen-refill.json`](checks/native-reopen-refill.json).
- F1 metadata-only: Value/MPN обновлены до `0997015.WXN`, а посадка, 4 площадки, координаты, сети, 639 сегментов, 146 переходов и 27 зон сохранены — [`checks/f1-metadata-preservation.json`](checks/f1-metadata-preservation.json).
- Проверка выполнялась из полного каталога проекта, поэтому проектные `fp-lib-table`/`sym-lib-table` были доступны. Отдельная сводка зависимостей — [`checks/dependency-audit.json`](checks/dependency-audit.json).
- Отрицательный контроль с удалением одного сегмента `/TC_SDI_MOSI` дал 2 нарушения и 1 несоединение; он не сохранён в комплекте. Применённый результат и отклонённые пробы — [`../../docs/research/VIA_REDUCTION_APPLIED_CURRENT.json`](../../docs/research/VIA_REDUCTION_APPLIED_CURRENT.json).
- Сверловка проверена по нативным объектам PCB: PTH 253 = 107 отверстий площадок + 146 переходов; NPTH 17. Все 270 отверстий совпали с раздельными Excellon по координатам, диаметрам и металлизации — [`../../docs/research/CAM_DRILL_GEOMETRY_FINAL.json`](../../docs/research/CAM_DRILL_GEOMETRY_FINAL.json).
- Та же машинная проверка подтверждает все 13 строк [`../../docs/BOM_MECHANICAL_INTERFACES_DRAFT.csv`](../../docs/BOM_MECHANICAL_INTERFACES_DRAFT.csv) относительно нативного контура: `X_from_left_mm=X_mm+5.000`, `Y_from_top_mm=Y_mm-10.000`. Размерный PDF пересобран из этой же PCB/CSV с базой `(-5.000; 10.000)`: новый PDF даёт 13/13 совпадений, а отрицательный контроль старого PDF — 13/13 ожидаемых расхождений. Текстовая и визуальная проверка — [`checks/mechanical-pdf-verification.json`](checks/mechanical-pdf-verification.json); PCB и отверстия не перемещались.
- Все 72 посадочных места находятся на F.Cu. В `cpl.csv` оставлены ровно 59 электрических позиций из BOM, все `Side=top`; крепёжные отверстия исключены.

## Состав

- [`gerbers/`](gerbers/) — F/B copper, mask, paste, silkscreen, Edge.Cuts и job-файл;
- [`drill/`](drill/) — раздельные `*-PTH.drl` и `*-NPTH.drl`, отдельные карты PDF и отчёт по отверстиям; смешанного Excellon в текущем каталоге нет;
- [`pdf/Sirokko26201_full_schematic.pdf`](pdf/Sirokko26201_full_schematic.pdf) — полная пятистраничная иерархическая схема;
- [`pdf/Sirokko26201_assembly_front.pdf`](pdf/Sirokko26201_assembly_front.pdf) и [`pdf/Sirokko26201_assembly_back.pdf`](pdf/Sirokko26201_assembly_back.pdf) — сборочные виды с позиционными обозначениями, площадками и полярностью;
- [`pdf/Sirokko26201_mechanical_holes.pdf`](pdf/Sirokko26201_mechanical_holes.pdf) — контур, механические элементы и отверстия;
- [`bom.csv`](bom.csv) — 59 электрических позиций с MPN; C109 остаётся `33nF 100V C0G 5%`, Murata `GRM2195C2A333JE01J`;
- [`cpl.csv`](cpl.csv) — 59 электрических позиций, все сверху;
- [`renders/`](renders/) — сборка спереди/сзади, 3D сверху/снизу и текущий рисунок F.Cu/B.Cu в PNG/SVG;
- [отчёт об изображениях меди](../../docs/research/COPPER_IMAGES_CURRENT_RU.md) и [машинная проверка](checks/copper-images-current.json);
- [`manifest.json`](manifest.json) — SHA-256 всех файлов текущего комплекта и исходных CAD.

Нативный экспорт BOM из KiCad 10.0.8 на этой иерархии аварийно завершился `bad_any_cast`; поэтому `bom.csv` сохранён из уже проверенного экспорта той же точной пары SCH/PCB и повторно сверен с 59 позициями PCB/CPL. Остальные файлы пересобраны заново.

Визуально проверены все пять страниц схемы, сборочные PDF, механический PDF, обе карты сверловки, увеличенный фрагмент нижнего левого узла, четыре сборочных/3D-изображения и два растровых изображения меди. Плата и карты помещаются целиком; на B.Cu компонентов нет. Нумерация схемы 1…5, обязательные подписи и размерный механический лист сохранены.

## Ограничения

Пять ссылок на необязательные стандартные 3D-модели отсутствуют в установленном KiCad 10.0.8; электрические зависимости и все проектные библиотеки разрешены, 3D-виды создаются, но не доказывают полную геометрию этих пяти корпусов. F1 — Littelfuse `0997015.WXN`, 15 А / 58 В DC, но токовая и тепловая оболочка остаётся проектной до физической квалификации первого изделия. Не проводились питание, прошивка, огневые, тепловые, механические, токовые и ресурсные испытания. Комплект не является разрешением заказа или выпуска.
