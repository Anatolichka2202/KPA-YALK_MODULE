# Развёртывание КТМА на Windows 11

Этот runbook обязателен перед переносом сборки на стенд.

Агент не должен изобретать альтернативный транспорт, если этот способ
работает.

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

Эти адреса не взаимозаменяемы.

Перед удалёнными действиями проверять именно hostname.

---

# Ключевое правило

На стенде:

```text
Qt НЕ нужен
compiler НЕ нужен
CMake НЕ нужен
build tree НЕ нужен
```

На стенд доставляется готовый release directory.

Сборка выполняется на машине разработчика.

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

Использовать существующий SSH/SCP flow.

Если он не работает:

```text
сначала диагностика существующего flow
```

а не создание новой инфраструктуры.

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

Диагностика собирает:

- Windows;
- сеть;
- маршруты;
- ARP;
- USB/PnP;
- COM;
- LCard/E20;
- VISA;
- DLL;
- SSH;
- пассивную UDP-диагностику adapter.

Результат:

```text
ZIP
SHA-256
```

---

# Сборка release

Канонические правила сборки:

[../product/build.md](../product/build.md)

CLI release flow:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release

cmake --build build -j 4

ctest --test-dir build --output-on-failure

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\package_stand_win11.ps1
```

Packaging создаёт новый release, ZIP и SHA-256 в:

```text
build\deploy
```

Существующий release с тем же именем не перезаписывать.

---

# Состав release

Типовой каталог:

```text
OrbitaDesktop.exe
orbita_equipment_probe.exe

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

Конкретный состав определяет packaging script.

Не собирать release вручную копированием DLL "пока не запустится".

---

# Установка

Каждый выпуск:

```text
C:\Orbita\releases\<version>
```

Новый выпуск всегда идёт в новый каталог.

Предыдущий release не перезаписывается.

Это rollback point.

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

SSH используется для:

```text
доставки
диагностики
CLI probe
```

`OrbitaDesktop.exe` штатно запускается оператором в desktop session стенда.

Не тратить время на попытку поднять GUI через служебный SSH-сеанс.

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
