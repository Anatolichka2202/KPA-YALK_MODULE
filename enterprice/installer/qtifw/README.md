# Qt Installer Framework bootstrap packaging — template only

`config/config.xml` и `packages/org.miltech.station.bootstrap/meta/package.xml` — каркас начальной установки. **Не готовый установщик**: в `data/` не положен собранный `StationBootstrap.exe` и Windows service, бинарники пока не разработаны. До их появления `binarycreator` не должен использовать этот шаблон для реальной установки.

### Запланированный процесс

1. Скомпилировать независимый `StationBootstrap.exe` и управляющий service с правами на `Program Files/MilTech/Station/releases`.
2. Выполнить Windows VM smoke test (запуск, health, rollback, Restore, uninstall с сохранением persistent data).
3. Поместить подписанные Authenticode binaries в `packages/org.miltech.station.bootstrap/data/`.
4. Создать offline installer Qt IFW `binarycreator --offline-only --config config/config.xml --packages packages StationInstaller.exe`.
5. Установить machine trust roots и `C:\ProgramData\MilTech\Station` с утверждёнными ACL; применять OS networking provisioning только в отдельном административном шаге.
6. После bootstrap provisioning runtime releases можно получать как подписанные `.mtbundle` с Enterprise или USB.

Обновление bootstrap, VC++ runtime, vendor drivers, изменения Windows Firewall/сетевых интерфейсов — **не** операции пользовательского runtime updater. Они требуют отдельного инсталлятора, повышения привилегий, сервисного окна и регламентированного rollback.
