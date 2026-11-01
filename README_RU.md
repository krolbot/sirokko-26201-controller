# Sirokko 262.01 — контроллер автоматики

> **DRAFT / REVIEW REQUIRED / NOT FOR ORDER / NO POWER / NO FLASH / NO FIRE; firmware HOLD.** Это не производственный выпуск и не доказательство работоспособности устройства. Плату нельзя заказывать, включать, прошивать или подключать к горелке.

В репозитории оставлен один канонический проект KiCad:

- проект: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pro);
- корневая схема: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_sch);
- PCB: [`hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb`](hardware/kicad/Sirokko26201_control_motor_candidate.kicad_pcb);
- SHA-256 PCB: `bdc72ee69a1cfd1b19614f053f4a46525d647ff002e5968d7c4d61195b102c20`;
- контур платы: 305 × 170 мм.

Колодки съёмного ESP32 DevKit находятся в J1=(100,45; 100,9) мм и J3=(100,45; 75,5) мм, обе 90° на B.Cu. Ветвь `+5V_DEVKIT` по прежней оси расширена: 76,45 мм до 2,0 мм и последние 2,0 мм до 1,2 мм; переходы и размещение не менялись. J30/J31 получили только условные 3D-модели колодок 1×9. Текущий состав: 70 посадочных мест, 287 площадок, 616 сегментов, 140 переходов и 27 зон. Свежий полный DRC KiCad 10.0.8 с перезаливкой зон, всеми ошибками дорожек и сверкой со схемой: 0 геометрических нарушений, 0 незамкнутых соединений, код 5 только из-за двух известных отсутствующих на PCB элементов R108/C109; `ignored_checks` пуст, SHA PCB до и после одинаков. Это только проверки CAD.

Эта ревизия — честный промежуточный WIP: новое прямое требование устанавливать ESP32 и все колодки на лицевой стороне реле ещё не применено. J1/J3 остаются единственными посадочными местами на B.Cu; их перенос требует отражения пары целиком и новой местной разводки. Подготовительный нативный расчёт сохранён в [`docs/research/ESP_FRONT_SIDE_CURRENT.json`](docs/research/ESP_FRONT_SIDE_CURRENT.json), но он не является новой PCB или пройденной разводкой.

Актуальные изображения из этой PCB:

- [вид спереди](docs/board-front.png);
- [вид сзади](docs/board-back.png), зеркально со стороны обратного слоя;
- [наклонный 3D-вид](docs/board-3d-oblique.png);
- [3D-вид снизу](docs/board-3d-bottom.png).

Инженерная передача расширения питания для родительской PCB `4346aff8…`: [`docs/research/ESP_POWER_BRANCH_CURRENT_RU.md`](docs/research/ESP_POWER_BRANCH_CURRENT_RU.md). Последующая модельная ревизия и свежие изображения `bdc72ee6…`: [`docs/research/ESP_RELAYOUT_CURRENT.json`](docs/research/ESP_RELAYOUT_CURRENT.json). Текущий полный отчёт DRC: [`docs/research/ESP_POWER_BRANCH_FULL_DRC.json`](docs/research/ESP_POWER_BRANCH_FULL_DRC.json). Действующие требования: [`docs/REQUIREMENTS_RU.md`](docs/REQUIREMENTS_RU.md). Текущее состояние и ограничения: [`docs/CURRENT_STATE_RU.md`](docs/CURRENT_STATE_RU.md). Производственного комплекта и выпуска прошивки нет. Старые проекты, кандидаты, ревизионные копии, CAM-архивы и устаревшие экспорты платы в рабочем дереве не хранятся; восстановление прежних состояний — через историю Git.
