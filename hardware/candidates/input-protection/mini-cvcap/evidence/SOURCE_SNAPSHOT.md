# Проверка устойчивого снимка

## Граница

Проверялись только существующие авторские файлы в постоянной области проекта Lab. Схема, PCB, библиотеки, контейнер и службы на Lab не изменялись.

Проверено, что постоянный каталог проекта хоста является bind-mount рабочего каталога проекта контейнера KiCad. Один и тот же набор файлов был прочитан с хоста и из контейнера; размеры и SHA-256 совпали.

## Устойчивость копирования

Для 12 выбранных файлов рассчитаны SHA-256:

1. перед копированием на Lab;
2. после копирования на Lab;
3. у локальной копии.

Все три манифеста побайтно совпали. SHA-256 каждого манифеста: `d5a9c20f64ec183df6311a4141bf54256921815e921757962816a9b848ab352f`.

Исходные SHA-256:

```text
2de526c4caa239bcabee1402067e3418c37932fa7c2b20239a00049213749b5e  input_protection_MINI_DRAFT.kicad_sch
2d6d39cb3260dda1298d91e9217db32365b9bce7066327d67f9e358174d40ed6  input_protection_MINI_DRAFT.kicad_pro
8c39a7d6ecbe1d27efd030bfa6a137775b8f93d0786833cd240fb240db681362  input_protection_MINI_DRAFT.kicad_pcb
8f825f65a45f2a58bc7436b54a16e99418f994301dd2e11406aa3a88c9bfacca  input_protection_CANDIDATE.kicad_sym
cf33875fd6db38dcb2133e0b7f30ce571f4f16a716bb72beb33987b69c188f5b  sym-lib-table
f8689f647c10d1f71bae74843ec08e9e82f4a702ff969f8f787f2e133e6d5d76  fp-lib-table
27f65864bc168b4c158d426d6fcb3ad4b6147478605c1bd60707c7998d8c0d43  Sirokko_Input.pretty/Keystone_3568_MINI_Blade_DRAFT.kicad_mod
9a83bcdb73d6be4c2cce568ec90dc1cffb9fae97185dc0330335d3cec916bd1a  cvcap_before/input_protection_MINI_DRAFT.xml
a91f0f0649d151838123eb8afec37fe787f098b247aa170679ea7ec81ea9ede3  cvcap_after/input_protection_MINI_DRAFT.xml
717de832583eca2c2229a8683ded84f89f9e274d1d618951c0189374c564316f  cvcap_after/input_protection_MINI_DRAFT-bom.csv
fcab5c8663864be0a9b18cab775edeb087490d1aeb1309b0a6843d16db533e43  cvcap_after/input_protection_MINI_DRAFT.pdf
0ebf5a803c3b36de466dbb6552bc71f203733aaead35dd3c314b10934decf74f  cvcap_after/input_protection_MINI_DRAFT-erc.json
```

## Очистка для публикации

В обоих публикуемых XML абсолютное поле `design/source` заменено относительным `../native/input_protection_MINI_DRAFT.kicad_sch`. Остальное содержимое XML не менялось. Поэтому публикуемые XML имеют новые SHA-256, а исходные хеши выше оставлены для трассируемости.

## Netlist и C1

`cvcap_before` и `cvcap_after` различаются только временем экспорта и свойствами C1. Разделы `nets` совпадают. Отдельные полные карты `pin-net-before.json` и `pin-net-after.json` также побайтно одинаковы: 23 вывода.

Точное состояние C1 после изменения:

- Value: `220nF 50V C0G 5%; VCAP-ANODE`
- MPN: `GRM31C5C1H224JE02L`
- Footprint: `Capacitor_SMD:C_1206_3216Metric`
- `C1.1 -> /VCAP_LOCAL`, вместе с `U1.1`
- `C1.2 -> /VIN_FUSED`, вместе с `U1.6 (ANODE)`

## ERC, BOM и PDF

Сохранённый ERC-отчёт относится к KiCad 10.0.7, содержит `ignored_checks=[]`, включает severity `error`, `warning`, `exclusion` и не содержит нарушений. Это чтение сохранённого отчёта, не новый запуск ERC.

BOM содержит 7 компонентов. PDF содержит один непустой лист A4 и открывается, но имеет наложения подписей в центральной части и историческое примечание о nominal 100nF. Текущая величина C1 на символе — 220 nF; связность подтверждена netlist, а не визуальным чтением PDF.
