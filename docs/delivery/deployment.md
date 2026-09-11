# Развёртывание КТМА на Windows 11

Этот runbook обязателен перед переносом сборки на стенд.

Агент не должен изобретать альтернативный транспорт, если этот способ работает.

---

# Две сети/роли адресов

Интерфейс ПЭВМ для оборудования:

```text
192.168.0.50
```

Подтверждённый SSH/SCP-адрес той же ПЭВМ:

```text
192.168.0.31
```

Имя машины:

```text
DESKTOP-5EO9J5A
```

Эти адреса не взаимозаменяемы. Перед удалёнными действиями проверять именно hostname.

---

# Ключевое правило

На стенде:

```text
Qt НЕ нужен
compiler НЕ нужен
CMake НЕ нужен
build tree НЕ нужен
```

На стенд доставляется готовый release directory. Сборка выполняется на машине разработчика или CI.

---

# Каталог выпусков и rollback

Каждый выпуск устанавливается только в новый каталог:

```text
C:\Orbita\releases\MilTechStation-KTMA-2.0.0-pilot-<short-sha>
```

Пример:

```text
C:\Orbita\releases\MilTechStation-KTMA-2.0.0-pilot-8ea034c
```

Существующий release не перезаписывается и не удаляется. Он является rollback point.

---

# Rolling pilot для ветки new-dis

Успешный CI ветки `new-dis` публикует rolling prerelease с фиксированным тегом:

```text
new-dis-latest
```

Фиксированные assets:

```text
MilTechStation-KTMA-new-dis.zip
MilTechStation-KTMA-new-dis.zip.sha256.txt
```

Внутри ZIP находится один каталог с фактическим commit-based именем:

```text
MilTechStation-KTMA-2.0.0-pilot-<short-sha>
```

Поэтому ссылка на скачивание постоянна, но установка всегда создаёт новый каталог и не затирает прошлый выпуск.

На стенде канонический install-flow:

```powershell
$u='https://raw.githubusercontent.com/Anatolichka2202/KPA-YALK_MODULE/new-dis/scripts/install_new_dis_release.ps1'
Invoke-WebRequest -UseBasicParsing $u -OutFile "$env:TEMP\install_new_dis_release.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:TEMP\install_new_dis_release.ps1"
```

Скрипт:

1. скачивает ZIP и SHA-256 из `new-dis-latest`;
2. проверяет SHA-256;
3. проверяет, что ZIP содержит ровно один каталог `MilTechStation-KTMA-2.0.0-pilot-<short-sha>`;
4. отказывается перезаписывать существующий каталог;
5. устанавливает выпуск в `C:\Orbita\releases`.

Прямая ссылка на текущий ZIP:

```text
https://github.com/Anatolichka2202/KPA-YALK_MODULE/releases/download/new-dis-latest/MilTechStation-KTMA-new-dis.zip
```

---

# Запрет на самодеятельность агента

Для обычного переноса выпуска запрещено без отдельной прямой задачи:

```text
P2P network
Wi-Fi Direct
ad-hoc network
SMB setup
FTP server
temporary HTTP server
cloud sync
VPN
new routing
firewall reconfiguration
```

Для удалённой диагностики и ручной доставки использовать существующий SSH/SCP flow. Если он не работает — сначала диагностика существующего flow, а не создание новой инфраструктуры.

---

# Проверка стендовой машины

На предполагаемом стенде:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\check_stand_access_win11.ps1
```

Проверить:

```text
hostname = DESKTOP-5EO9J5A
SSH listener TCP/22
ожидаемый IPv4
```

Ping сам по себе не доказывает, что это правильная машина.

---

# Первичное включение SSH

Только если удалённый доступ ещё не настроен, один раз от администратора:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\enable_stand_remote_access_win11.ps1 `
  -UserName <пользователь-стенда> `
  -AllowedClientAddress <IP-компьютера-разработчика>
```

Закрытые ключи и пароли в Git не добавляются.

---

# Проверка с машины разработчика

```powershell
$standSshAddress = '192.168.0.31'

Test-NetConnection $standSshAddress -Port 22

.\scripts\stand_remote.ps1 `
  -StandAddress $standSshAddress `
  -Action Status

.\scripts\stand_remote.ps1 `
  -StandAddress $standSshAddress `
  -Action Shell
```

`stand_remote.ps1` не должен молча выбирать неизвестный адрес.

---

# Диагностика стенда

До установки:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\stand_full_diagnostics_win11.ps1 `
  -OutputDirectory C:\Users\Public\OrbitaDiag
```

Диагностика собирает Windows, сеть, маршруты, ARP, USB/PnP, COM, LCard/E20, VISA, DLL, SSH и пассивную UDP-диагностику adapter.

Результат: ZIP и SHA-256.

---

# Сборка release

Канонические правила сборки:

[../product/build.md](../product/build.md)

CLI release flow:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_stand_win11.ps1
```

Packaging создаёт новый release, ZIP и SHA-256 в `build\deploy`. Существующий release с тем же именем не перезаписывается.

---

# Состав release

Типовой каталог:

```text
MilTechStation.exe
orbita_equipment_probe.exe
orbita_yvp_rokt_probe.exe
parameters.db
address\...
catalog\catalog.yaml
profiles\stand_ktma.yaml
scenarios\...
plugins\orbita_plugin_*.dll
Qt6*.dll
platforms\qwindows.dll
Lusbapi64.dll
```

Конкретный состав определяет packaging script. Не собирать release вручную копированием DLL «пока не запустится».

---

# Первый запуск нового release

1. `active_outputs_confirmed: false`.
2. Выполнить диагностику.
3. Выполнить безопасные probe оборудования.
4. Проверить `TODO_CONFIRM`.
5. Проверить физические маршруты вручную.
6. Только после подтверждения разрешить active outputs.
7. Сначала диагностический run.
8. Затем полный run УБСИ.

---

# GUI через SSH

SSH используется для доставки, диагностики и CLI probe.

`MilTechStation.exe` штатно запускается оператором в desktop session стенда. Не тратить время на попытку поднять GUI через служебный SSH-сеанс.

---

# Если перенос не работает

Правильный порядок:

```text
1. проверить hostname
2. проверить SSH address
3. Test-NetConnection :22
4. stand_remote.ps1 -Action Status
5. проверить sshd
6. только потом чинить существующий SSH flow
```

Не начинать с проектирования новой сети.
