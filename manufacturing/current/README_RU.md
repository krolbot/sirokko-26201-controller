# Sirokko — единый текущий проверочный комплект

**DRAFT / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE.**
`production_release=false`; `physical_tests=false`; прошивка остаётся на HOLD.

## Источник

Производные файлы ниже заново получены штатным KiCad 10.0.8 на Lab непосредственно из канонической пары, опубликованной в `main` на коммите [`eadad5de02062fea6009871ee03c63b9a983c735`](https://github.com/krolbot/sirokko-26201-controller/commit/eadad5de02062fea6009871ee03c63b9a983c735):

- PCB SHA-256: `46b84ef09d00a724962753ac0e83492b63a43177d69b33a519c890d5be9a0820`;
- корневая схема SHA-256: `c8c5bd6e2bc0cb61428037c93b54b6c5d26ea97d9e6529f3facb100935ee3b3b`;
- проект SHA-256: `11029d86464484cea63d73aa802bad95c1a6805aee11e841fc78322e9d2bc0d6`.

Исходные PCB/SCH не пересохранены. До и после экспорта SHA совпали. В `hardware/kicad/` находится одна каноническая PCB/PRO, полная иерархия схемы, локальные таблицы библиотек, проектные символы, посадочные места и модели. Параллельных PCB и снимков в текущем комплекте нет.

## Проверки

- Root ERC: код 0, 0 нарушений — [`checks/erc.json`](checks/erc.json).
- Native DRC с `--refill-zones --schematic-parity --severity-all --exit-code-violations`: код 0, 0 нарушений, 0 несоединений, 0 расхождений PCB↔SCH — [`checks/drc.json`](checks/drc.json).
- Отдельная распаковка только `hardware/kicad/` в чистый каталог повторно дала ERC 0 и DRC 0/0/0 при тех же SHA PCB/SCH — [`project-opencheck-erc.json`](checks/project-opencheck-erc.json), [`project-opencheck-drc.json`](checks/project-opencheck-drc.json).
- Проверка выполнялась из полного каталога проекта, поэтому проектные `fp-lib-table`/`sym-lib-table` были доступны. Отдельная сводка зависимостей — [`checks/dependency-audit.json`](checks/dependency-audit.json).
- Staged Pre-fab Gate сохранил `ignored_checks=[]`, `excluded=0`, parity `ran=true`, но выдал 20 предупреждений `lib_footprint_issues` из-за своей изолированной конфигурации и предупреждение о неизвестной кривой срабатывания F1. Его `passed=true` **не используется** как доказательство доступности библиотек — [`checks/prefab-gate-staged.json`](checks/prefab-gate-staged.json).
- Сверловка проверена по нативным объектам PCB: PTH 254 = 107 отверстий площадок + 147 переходов; NPTH 17. Распределение по каждому диаметру полностью совпало с двумя Excellon — [`checks/drill-parity.json`](checks/drill-parity.json).
- Все 72 посадочных места находятся на F.Cu. В `cpl.csv` оставлены ровно 59 электрических позиций из BOM, все `Side=top`; крепёжные отверстия исключены.

## Состав

- [`gerbers/`](gerbers/) — F/B copper, mask, paste, silkscreen, Edge.Cuts и job-файл;
- [`drill/`](drill/) — раздельные `*-PTH.drl` и `*-NPTH.drl`, отдельные карты PDF и отчёт по отверстиям; смешанного Excellon в текущем каталоге нет;
- [`pdf/Sirokko26201_full_schematic.pdf`](pdf/Sirokko26201_full_schematic.pdf) — полная пятистраничная иерархическая схема;
- [`pdf/Sirokko26201_assembly_front.pdf`](pdf/Sirokko26201_assembly_front.pdf) и [`pdf/Sirokko26201_assembly_back.pdf`](pdf/Sirokko26201_assembly_back.pdf) — сборочные виды с позиционными обозначениями, площадками и полярностью;
- [`pdf/Sirokko26201_mechanical_holes.pdf`](pdf/Sirokko26201_mechanical_holes.pdf) — контур, механические элементы и отверстия;
- [`bom.csv`](bom.csv) — 59 электрических позиций с MPN; C109 остаётся `33nF 100V C0G 5%`, Murata `GRM2195C2A333JE01J`;
- [`cpl.csv`](cpl.csv) — 59 электрических позиций, все сверху;
- [`renders/`](renders/) — актуальные лицевой/обратный, сборочные и 3D-виды;
- [`manifest.json`](manifest.json) — SHA-256 всех файлов текущего комплекта и исходных CAD.

Нативный экспорт BOM из KiCad 10.0.8 на этой иерархии аварийно завершился `bad_any_cast`; поэтому `bom.csv` сохранён из уже проверенного экспорта той же точной пары SCH/PCB и повторно сверен с 59 позициями PCB/CPL. Остальные файлы пересобраны заново.

Визуально проверены все пять страниц схемы, сборочные PDF, механический PDF, обе карты сверловки и семь изображений. Плата и карты помещаются целиком; на B.Cu компонентов нет. На листе 2 схемы существующее синее примечание частично выходит за рамку A4. Источник не передвигался ради оформления; это известное ограничение PDF текущей схемы, а не потеря листа.

## Ограничения

Пять ссылок на необязательные стандартные 3D-модели отсутствуют в установленном KiCad 10.0.8; электрические зависимости и все проектные библиотеки разрешены, 3D-виды создаются, но не доказывают полную геометрию этих пяти корпусов. Полная токовая квалификация F1 не выполнена: номинал 30 A не является кривой срабатывания. Не проводились питание, прошивка, огневые, тепловые, механические, токовые и ресурсные испытания. Комплект не является разрешением заказа или выпуска.
