from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "apps/desktop/ktma_mainwindow.cpp",
    "#include <QApplication>\n",
    "#include <QAction>\n#include <QApplication>\n",
)
replace_once(
    "apps/desktop/ktma_mainwindow.cpp",
    "#include <QMessageBox>\n",
    "#include <QMessageBox>\n#include <QMenu>\n",
)

replace_once(
    "plans/active/universal-miltechstation-backend.md",
    "- [x] регистрация KTMA/UBSI procedures и discovery TU-сценариев вынесены из reusable `MainWindow` в `KtmaMainWindow`.\n\nОстаётся:\n\n- [ ] вынести KTMA registrar/profile bootstrap из `MainWindow` в delivery/application composition;\n",
    "- [x] регистрация KTMA/UBSI procedures и discovery TU-сценариев вынесены из reusable `MainWindow` в `KtmaMainWindow`;\n"
    "- [x] выбор/загрузка `stand_ktma.yaml` принадлежит KTMA application composition; reusable `MainWindow` получает уже настроенный `StandProfile`;\n"
    "- [x] KTMA `Registrar` и `registrar.db` создаются и принадлежат `KtmaMainWindow`; generic scenario runner/report finalizer больше не содержит production lifecycle регистратора.\n\n"
    "Остаётся:\n\n",
)
replace_once(
    "plans/active/universal-miltechstation-backend.md",
    "1. держать каждый backend-срез зелёным по CI;\n2. вынести оставшийся KTMA registrar/profile bootstrap из reusable `MainWindow`;\n3. закончить resource migration standalone YTP и остальных TU-сценариев после resource-aware test fixture;\n4. перевести оставшиеся внутренние KTMA includes на канонические `ktma/ubsi/*` и убрать wrappers после последнего consumer;\n5. связать `execution_runtime` с run/evidence и подключить первый существующий Python/Lua стенд;\n6. после появления реального board consumer добавить serial/SSH transport contracts.\n",
    "1. держать каждый backend-срез зелёным по CI;\n2. сокращать защищённый `integration*()` API и постепенно заменить наследование product/delivery composition;\n3. закончить resource migration standalone YTP и остальных TU-сценариев после resource-aware test fixture;\n4. перевести оставшиеся внутренние KTMA includes на канонические `ktma/ubsi/*` и убрать wrappers после последнего consumer;\n5. связать `execution_runtime` с run/evidence и подключить первый существующий Python/Lua стенд;\n6. после появления реального board consumer добавить serial/SSH transport contracts.\n",
)

print("Composition follow-up applied")
