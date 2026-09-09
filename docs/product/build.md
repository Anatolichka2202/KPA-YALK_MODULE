# Сборка MilTechStation

Этот документ — обязательная точка входа перед любой сборкой.

Агент не должен заново исследовать тулчейн, устанавливать новые средства
сборки или менять способ сборки без явной задачи.

---

# Канонический рабочий процесс

Для обычной разработки используется:

```text
Qt Creator
  ↓
существующий настроенный Kit
  ↓
CMake
  ↓
build/
```

Человеческий рабочий процесс:

1. открыть корневой `CMakeLists.txt` в Qt Creator;
2. выбрать уже настроенный рабочий Kit;
3. использовать каталог сборки проекта;
4. Configure;
5. Build;
6. при необходимости Run/Debug.

---

# Главное правило для агента

Агент НЕ выбирает новый compiler/Qt/toolchain "по своему вкусу".

Если проект уже собирается в Qt Creator:

```text
существующий Kit
=
источник истины для локального toolchain
```

При CLI-сборке агент должен брать параметры из уже существующей рабочей
конфигурации:

- Qt Creator Kit;
- `CMakeCache.txt`;
- существующего build directory;
- project CMake files.

Не искать другой тулчейн в интернете.

Не устанавливать автоматически:

```text
новый Qt
Visual Studio
MinGW
Ninja
vcpkg
Conan
Chocolatey packages
MSYS
WSL
```

если это не является отдельной прямой задачей.

---

# CLI-эквивалент текущей release-сборки

Текущий deployment-runbook использует:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

После тестов:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\package_stand_win11.ps1
```

Если Qt Creator использует иной generator/configuration mode, CLI-команда
должна соответствовать существующему Kit, а не создавать параллельный
самодельный toolchain.

---

# Каталог build

Сгенерированные файлы сборки не являются исходниками и не должны
размазываться по репозиторию.

Разрешённый смысл:

```text
source tree
  +
один явный build directory
```

Запрещено создавать случайные:

```text
build2
build_new
build_final
build_test123
cmake-build-whatever
```

только потому, что предыдущая команда не удалась.

Сначала разобраться с существующей сборкой.

---

# Что запрещено коммитить

Не добавлять в Git:

- CMake cache;
- object files;
- generated Qt files;
- IDE state;
- release ZIP;
- DLL из build/output;
- временные deployment copies;
- локальные toolchain paths.

---

# Сборка и deployment — разные задачи

```text
сборка
  =
получить проверенный release

deployment
  =
доставить готовый release на стенд
```

На стенд не переносится build tree.

Deployment:

[../delivery/deployment.md](../delivery/deployment.md)
