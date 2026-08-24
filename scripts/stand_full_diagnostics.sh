#!/usr/bin/env bash
# Полная безопасная диагностика стендового компьютера.
# Скрипт не изменяет настройки и не отправляет команды подключённым приборам.

set -u
umask 077

SCRIPT_VERSION="1.0"
EXPECTED_STAND_IP="192.168.0.50"
OUTPUT_PARENT="${1:-$PWD}"

mkdir -p "$OUTPUT_PARENT" || {
    echo "Не удалось создать каталог: $OUTPUT_PARENT" >&2
    exit 1
}
OUTPUT_PARENT="$(cd "$OUTPUT_PARENT" && pwd -P)"

STAMP="$(date +%Y%m%d_%H%M%S)"
HOST_SAFE="$(hostname 2>/dev/null | tr -cd '[:alnum:]_.-' | head -c 64)"
[[ -n "$HOST_SAFE" ]] || HOST_SAFE="stand"
REPORT_NAME="orbita_stand_diag_${HOST_SAFE}_${STAMP}"
REPORT_DIR="$OUTPUT_PARENT/$REPORT_NAME"
mkdir -p "$REPORT_DIR" || exit 1

SUMMARY="$REPORT_DIR/00_SUMMARY.txt"
SYSTEM_REPORT="$REPORT_DIR/10_SYSTEM.txt"
SOFTWARE_REPORT="$REPORT_DIR/20_SOFTWARE.txt"
USB_REPORT="$REPORT_DIR/30_USB.txt"
SERIAL_REPORT="$REPORT_DIR/40_SERIAL_RS485.txt"
DAQ_REPORT="$REPORT_DIR/50_ADC_LCOMP_VISA.txt"
NETWORK_REPORT="$REPORT_DIR/60_NETWORK_SSH.txt"
KERNEL_REPORT="$REPORT_DIR/70_KERNEL_UDEV.txt"
SERVICES_REPORT="$REPORT_DIR/80_SERVICES.txt"
FULL_LOG="$REPORT_DIR/99_RUN_LOG.txt"

touch "$SUMMARY" "$SYSTEM_REPORT" "$SOFTWARE_REPORT" "$USB_REPORT" \
      "$SERIAL_REPORT" "$DAQ_REPORT" "$NETWORK_REPORT" "$KERNEL_REPORT" \
      "$SERVICES_REPORT" "$FULL_LOG"
cp -- "$0" "$REPORT_DIR/stand_full_diagnostics.sh" 2>/dev/null || true

exec > >(tee -a "$FULL_LOG") 2>&1

have() {
    command -v "$1" >/dev/null 2>&1
}

status_line() {
    local state="$1"
    shift
    printf '[%-4s] %s\n' "$state" "$*" | tee -a "$SUMMARY"
}

run_capture() {
    local report="$1"
    local title="$2"
    shift 2
    {
        printf '\n===== %s =====\n' "$title"
        printf '$'
        printf ' %q' "$@"
        printf '\n'
        if have timeout; then
            timeout 20s "$@"
        else
            "$@"
        fi
        local rc=$?
        printf '[exit=%s]\n' "$rc"
    } >>"$report" 2>&1 || true
}

run_shell() {
    local report="$1"
    local title="$2"
    local command_text="$3"
    {
        printf '\n===== %s =====\n' "$title"
        printf '$ %s\n' "$command_text"
        if have timeout; then
            timeout 20s bash -o pipefail -c "$command_text"
        else
            bash -o pipefail -c "$command_text"
        fi
        local rc=$?
        printf '[exit=%s]\n' "$rc"
    } >>"$report" 2>&1 || true
}

capture_if_present() {
    local report="$1"
    local title="$2"
    local executable="$3"
    shift 3
    if have "$executable"; then
        run_capture "$report" "$title" "$executable" "$@"
    else
        printf '\n===== %s =====\nКОМАНДА ОТСУТСТВУЕТ: %s\n' \
            "$title" "$executable" >>"$report"
    fi
}

printf 'Диагностика стенда «Орбита»\n' >"$SUMMARY"
printf 'Версия скрипта: %s\n' "$SCRIPT_VERSION" >>"$SUMMARY"
printf 'Время: %s\n' "$(date --iso-8601=seconds 2>/dev/null || date)" >>"$SUMMARY"
printf 'Узел: %s\n' "$(hostname -f 2>/dev/null || hostname)" >>"$SUMMARY"
printf 'Пользователь: %s (uid=%s)\n\n' "$(id -un)" "$(id -u)" >>"$SUMMARY"

echo "Сбор отчёта в $REPORT_DIR"

# ---------------------------------------------------------------------------
# ОС, ресурсы и сессия
# ---------------------------------------------------------------------------
run_shell "$SYSTEM_REPORT" "Дата, пользователь и ядро" \
    'date --iso-8601=seconds 2>/dev/null || date; id; groups; uname -a'
run_shell "$SYSTEM_REPORT" "Описание ОС" \
    'test -r /etc/os-release && cat /etc/os-release; test -r /etc/astra_version && cat /etc/astra_version; true'
capture_if_present "$SYSTEM_REPORT" "hostnamectl" hostnamectl
capture_if_present "$SYSTEM_REPORT" "Процессор" lscpu
capture_if_present "$SYSTEM_REPORT" "Память" free -h
capture_if_present "$SYSTEM_REPORT" "Файловые системы" df -hT
capture_if_present "$SYSTEM_REPORT" "Блочные устройства" lsblk -e 7 -o NAME,TYPE,SIZE,FSTYPE,MOUNTPOINTS,MODEL,SERIAL,TRAN
run_shell "$SYSTEM_REPORT" "Графическая сессия" \
    'printf "DISPLAY=%s\nWAYLAND_DISPLAY=%s\nXDG_SESSION_TYPE=%s\nDESKTOP_SESSION=%s\n" "${DISPLAY-}" "${WAYLAND_DISPLAY-}" "${XDG_SESSION_TYPE-}" "${DESKTOP_SESSION-}"'

# ---------------------------------------------------------------------------
# Средства сборки и установленные пакеты
# ---------------------------------------------------------------------------
{
    echo "===== Наличие команд ====="
    for tool in bash ssh scp rsync git gcc g++ clang clang++ cmake ninja make \
                qmake6 qtpaths6 pkg-config python3 pip3 lsusb lspci udevadm \
                journalctl dmesg ip ss nmcli dkms modinfo tar sha256sum; do
        if have "$tool"; then
            printf '%-16s %s\n' "$tool" "$(command -v "$tool")"
        else
            printf '%-16s НЕТ\n' "$tool"
        fi
    done
} >>"$SOFTWARE_REPORT" 2>&1

for tool in gcc g++ clang clang++ cmake ninja make qmake6 qtpaths6 pkg-config python3; do
    if have "$tool"; then
        run_shell "$SOFTWARE_REPORT" "Версия $tool" \
            "'$tool' --version 2>&1 | head -n 5"
    fi
done
run_shell "$SOFTWARE_REPORT" "Qt 6 через pkg-config" \
    'pkg-config --modversion Qt6Core Qt6Widgets Qt6Sql Qt6SerialPort 2>&1 || true'
run_shell "$SOFTWARE_REPORT" "Пакеты Debian/Astra по теме стенда" \
    'if command -v dpkg-query >/dev/null 2>&1; then dpkg-query -W -f="\${Package}\t\${Version}\n" 2>/dev/null | grep -Ei "(lcomp|lcard|visa|serial|modbus|qt6|usbutils|pciutils|openssh|cmake|ninja)" | sort; fi; true'
run_shell "$SOFTWARE_REPORT" "Пакеты RPM по теме стенда" \
    'if command -v rpm >/dev/null 2>&1; then rpm -qa | grep -Ei "(lcomp|lcard|visa|serial|modbus|qt6|usb|openssh|cmake|ninja)" | sort; fi; true'

# ---------------------------------------------------------------------------
# USB и PCI: полный перечень физически подключённых устройств
# ---------------------------------------------------------------------------
capture_if_present "$USB_REPORT" "USB-устройства" lsusb
capture_if_present "$USB_REPORT" "Дерево USB" lsusb -t
capture_if_present "$USB_REPORT" "PCI-устройства и драйверы" lspci -nnk
run_shell "$USB_REPORT" "USB из sysfs: VID PID изготовитель изделие серийный номер драйвер" '
for d in /sys/bus/usb/devices/*; do
    [[ -r "$d/idVendor" && -r "$d/idProduct" ]] || continue
    vendor=$(cat "$d/idVendor" 2>/dev/null)
    product=$(cat "$d/idProduct" 2>/dev/null)
    manufacturer=$(cat "$d/manufacturer" 2>/dev/null || true)
    name=$(cat "$d/product" 2>/dev/null || true)
    serial=$(cat "$d/serial" 2>/dev/null || true)
    driver=""
    [[ -L "$d/driver" ]] && driver=$(basename "$(readlink "$d/driver")")
    printf "%s\t%s:%s\t%s\t%s\tserial=%s\tdriver=%s\n" "$(basename "$d")" "$vendor" "$product" "$manufacturer" "$name" "$serial" "$driver"
done'
run_shell "$USB_REPORT" "HID, USBTMC, видео и прочие символьные USB-устройства" \
    'find /dev -maxdepth 2 \( -name "hidraw*" -o -name "usbtmc*" -o -name "video*" -o -name "media*" \) -printf "%M %u %g %p -> %l\n" 2>/dev/null | sort; true'

# ---------------------------------------------------------------------------
# Последовательные порты / физический адаптер RS-485
# ---------------------------------------------------------------------------
run_shell "$SERIAL_REPORT" "Все последовательные устройства" '
find /dev -maxdepth 1 \( -name "ttyS*" -o -name "ttyUSB*" -o -name "ttyACM*" -o -name "ttyAMA*" -o -name "ttyTHS*" -o -name "rfcomm*" \) -printf "%M %u %g %p -> %l\n" 2>/dev/null | sort; true'
run_shell "$SERIAL_REPORT" "Стабильные имена serial/by-id и serial/by-path" '
for dir in /dev/serial/by-id /dev/serial/by-path; do
    echo "--- $dir"
    if [[ -d "$dir" ]]; then ls -la "$dir"; else echo "нет"; fi
done'
run_shell "$SERIAL_REPORT" "udev-свойства последовательных устройств" '
command -v udevadm >/dev/null 2>&1 || exit 0
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [[ -e "$dev" ]] || continue
    echo "===== $dev ====="
    udevadm info --query=property --name="$dev" 2>/dev/null | grep -E "^(DEVNAME|DEVPATH|DRIVER|ID_BUS|ID_VENDOR|ID_VENDOR_ID|ID_MODEL|ID_MODEL_ID|ID_SERIAL|ID_USB_DRIVER|ID_PATH|MAJOR|MINOR)=" || true
done'
run_shell "$SERIAL_REPORT" "Группы пользователя для доступа к портам" \
    'id; getent group dialout 2>/dev/null || true; getent group uucp 2>/dev/null || true; getent group plugdev 2>/dev/null || true'
run_shell "$SERIAL_REPORT" "Загруженные драйверы USB-COM" \
    'lsmod 2>/dev/null | grep -Ei "(usbserial|ftdi_sio|cp210x|ch341|pl2303|cdc_acm|8250|serial)" || true'

# ---------------------------------------------------------------------------
# АЦП E20-10/LComp, VISA, USBTMC и другие подсистемы измерения
# ---------------------------------------------------------------------------
run_shell "$DAQ_REPORT" "Заголовки текущего ядра" '
kbuild="/lib/modules/$(uname -r)/build"
if [[ -d "$kbuild" ]]; then echo "OK $kbuild"; else echo "НЕТ $kbuild"; fi'
capture_if_present "$DAQ_REPORT" "DKMS" dkms status
run_shell "$DAQ_REPORT" "Модули LComp/L-Card/DAQ/VISA/USBTMC" \
    'lsmod 2>/dev/null | grep -Ei "(lcomp|lcard|e2010|usbtmc|comedi|iio|gpib)" || true'
run_shell "$DAQ_REPORT" "Информация о найденных модулях" '
command -v modinfo >/dev/null 2>&1 || exit 0
for module in lcomp lcard e2010 usbtmc comedi gpib_common; do
    if modinfo "$module" >/dev/null 2>&1; then echo "===== $module ====="; modinfo "$module"; fi
done'
run_shell "$DAQ_REPORT" "Библиотеки LComp и VISA" \
    'ldconfig -p 2>/dev/null | grep -Ei "(lcomp|lcard|libvisa|visa\.so|nivisa|ktvisa|rsvisa)" || true'
run_shell "$DAQ_REPORT" "Заголовки LComp и VISA" '
find /usr/include /usr/local/include /opt -maxdepth 5 -type f \( -iname "LDevice.h" -o -iname "*lcomp*.h" -o -iname "visa.h" -o -iname "visatype.h" \) -print 2>/dev/null | head -n 200; true'
run_shell "$DAQ_REPORT" "Узлы устройств АЦП и измерительных подсистем" '
find /dev -maxdepth 2 \( -iname "*lcomp*" -o -iname "*lcard*" -o -iname "*e2010*" -o -iname "*e20*" -o -iname "usbtmc*" -o -iname "comedi*" -o -iname "iio:device*" -o -iname "gpib*" \) -printf "%M %u %g %p -> %l\n" 2>/dev/null | sort; true'
run_shell "$DAQ_REPORT" "IIO-устройства" '
for d in /sys/bus/iio/devices/iio:device*; do
    [[ -d "$d" ]] || continue
    printf "%s name=%s\n" "$d" "$(cat "$d/name" 2>/dev/null || true)"
done'
run_shell "$DAQ_REPORT" "PyVISA" '
if command -v pyvisa-info >/dev/null 2>&1; then
    pyvisa-info
elif command -v python3 >/dev/null 2>&1; then
    python3 -m pyvisa info 2>&1 || true
fi'
run_shell "$DAQ_REPORT" "pkg-config: измерительные библиотеки" \
    'pkg-config --list-all 2>/dev/null | grep -Ei "(visa|lcomp|lcard|serial|modbus|gpib)" || true'

# ---------------------------------------------------------------------------
# Сеть и SSH. Активного сканирования сети нет.
# ---------------------------------------------------------------------------
capture_if_present "$NETWORK_REPORT" "Сетевые интерфейсы" ip -br link
capture_if_present "$NETWORK_REPORT" "IP-адреса" ip -br address
capture_if_present "$NETWORK_REPORT" "Маршруты" ip route show table all
capture_if_present "$NETWORK_REPORT" "Таблица соседей: возможные сетевые приборы" ip neigh show
capture_if_present "$NETWORK_REPORT" "Слушающие TCP/UDP-порты" ss -lntup
capture_if_present "$NETWORK_REPORT" "NetworkManager" nmcli device show
run_shell "$NETWORK_REPORT" "SSH-служба" '
if command -v systemctl >/dev/null 2>&1; then
    systemctl --no-pager --full status ssh.service 2>&1 || true
    systemctl --no-pager --full status sshd.service 2>&1 || true
fi
ss -lnt 2>/dev/null | grep -E "Local Address|:22([[:space:]]|$)" || true'
run_shell "$NETWORK_REPORT" "Конфигурация имён без секретов" \
    'cat /etc/resolv.conf 2>/dev/null || true; command -v resolvectl >/dev/null 2>&1 && resolvectl status 2>/dev/null || true'

# ---------------------------------------------------------------------------
# udev, службы и журналы подключения оборудования
# ---------------------------------------------------------------------------
run_shell "$KERNEL_REPORT" "Последние сообщения ядра" \
    'journalctl -k -b --no-pager -n 1200 2>/dev/null || dmesg 2>/dev/null | tail -n 1200 || true'
run_shell "$KERNEL_REPORT" "Сообщения ядра по оборудованию" \
    '(journalctl -k -b --no-pager 2>/dev/null || dmesg 2>/dev/null) | grep -Ei "(usb|tty|serial|ftdi|cp210|ch34|pl2303|lcomp|lcard|e20|visa|usbtmc|comedi|iio|gpib|firmware|driver|denied|error|fail)" | tail -n 1000 || true'
run_shell "$KERNEL_REPORT" "Правила udev по измерительному оборудованию" \
    'grep -RHiEn "(lcomp|lcard|e2010|usbtmc|visa|ttyUSB|ttyACM|ftdi|cp210|ch34|pl2303|gpib)" /etc/udev/rules.d /usr/lib/udev/rules.d /lib/udev/rules.d 2>/dev/null | head -n 500 || true'
run_shell "$SERVICES_REPORT" "Запущенные службы" \
    'systemctl list-units --type=service --state=running --no-pager --no-legend 2>/dev/null || true'
run_shell "$SERVICES_REPORT" "Процессы, похожие на драйверы и ПО стенда" \
    'ps -eo user,pid,ppid,stat,lstart,args --width 240 | grep -Ei "(orbita|lcomp|lcard|visa|serial|modbus|gpib|pyvisa|scpi)" | grep -v grep || true'

# ---------------------------------------------------------------------------
# Автоматическая сводка. Это эвристика; полный сырой перечень остаётся в файлах.
# ---------------------------------------------------------------------------
if [[ -r /etc/os-release ]]; then
    status_line OK "ОС определена: $(. /etc/os-release; echo "${PRETTY_NAME:-unknown}")"
else
    status_line WARN "Не найден /etc/os-release"
fi

if have ip && ip -br address 2>/dev/null | grep -Eq "(^|[[:space:]])${EXPECTED_STAND_IP}/"; then
    status_line OK "На компьютере назначен ожидаемый IP $EXPECTED_STAND_IP"
else
    status_line WARN "Ожидаемый IP $EXPECTED_STAND_IP не найден на интерфейсах"
fi

if have ss && ss -lnt 2>/dev/null | grep -Eq '(:|\])22([[:space:]]|$)'; then
    status_line OK "SSH слушает TCP/22"
else
    status_line WARN "SSH не слушает TCP/22"
fi

for required in g++ cmake; do
    if have "$required"; then status_line OK "Есть $required"; else status_line WARN "Нет $required"; fi
done
if have ninja || have make; then
    status_line OK "Есть система сборки ninja/make"
else
    status_line WARN "Нет ninja и make"
fi
if have qmake6 || have qtpaths6 || (have pkg-config && pkg-config --exists Qt6Core 2>/dev/null); then
    status_line OK "Qt 6 обнаружен"
else
    status_line WARN "Qt 6 не обнаружен"
fi

USB_COUNT=0
if have lsusb; then USB_COUNT="$(lsusb 2>/dev/null | wc -l)"; fi
if (( USB_COUNT > 0 )); then
    status_line OK "USB-устройств в lsusb: $USB_COUNT"
else
    status_line WARN "lsusb отсутствует или не вернул устройства"
fi

SERIAL_COUNT="$(find /dev -maxdepth 1 \( -name 'ttyUSB*' -o -name 'ttyACM*' \) 2>/dev/null | wc -l)"
if (( SERIAL_COUNT > 0 )); then
    status_line OK "Найдено USB-COM/RS-485 портов: $SERIAL_COUNT"
else
    status_line WARN "USB-COM/RS-485 порты ttyUSB/ttyACM не найдены"
fi

if id -nG 2>/dev/null | tr ' ' '\n' | grep -Eq '^(dialout|uucp|plugdev)$' || [[ "$(id -u)" == "0" ]]; then
    status_line OK "У пользователя вероятно есть права на последовательные устройства"
else
    status_line WARN "Пользователь не состоит в dialout/uucp/plugdev"
fi

if lsmod 2>/dev/null | grep -Eqi '(lcomp|lcard|e2010)' || \
   ldconfig -p 2>/dev/null | grep -Eqi '(lcomp|lcard)' || \
   find /dev -maxdepth 2 \( -iname '*lcomp*' -o -iname '*lcard*' -o -iname '*e2010*' \) 2>/dev/null | grep -q .; then
    status_line OK "Найдены признаки установленного LComp/E20-10"
else
    status_line WARN "Драйвер, библиотека или узел LComp/E20-10 не обнаружены"
fi

if ldconfig -p 2>/dev/null | grep -Eqi '(libvisa|nivisa|ktvisa|rsvisa)' || \
   have pyvisa-info || (have python3 && python3 -c 'import pyvisa' >/dev/null 2>&1); then
    status_line OK "Обнаружен VISA или PyVISA"
else
    status_line WARN "VISA/PyVISA не обнаружены"
fi

USBTMC_COUNT="$(find /dev -maxdepth 1 -name 'usbtmc*' 2>/dev/null | wc -l)"
if (( USBTMC_COUNT > 0 )); then
    status_line OK "Найдено USBTMC-приборов: $USBTMC_COUNT"
else
    status_line WARN "Узлы /dev/usbtmc* не найдены"
fi

AVAILABLE_KB="$(df -Pk "$OUTPUT_PARENT" 2>/dev/null | awk 'NR==2 {print $4}')"
if [[ "$AVAILABLE_KB" =~ ^[0-9]+$ ]] && (( AVAILABLE_KB >= 2097152 )); then
    status_line OK "Свободного места для сборки больше 2 ГБ"
else
    status_line WARN "Свободного места меньше 2 ГБ или объём не определён"
fi

{
    echo
    echo "Файлы полного отчёта:"
    find "$REPORT_DIR" -maxdepth 1 -type f -printf '  %f\n' | sort
    echo
    echo "Важно: WARN означает 'нужно проверить', а не обязательно неисправность."
} >>"$SUMMARY"

# Контрольные суммы и единый архив для передачи разработчику.
if have sha256sum; then
    (
        cd "$REPORT_DIR" || exit 0
        find . -maxdepth 1 -type f ! -name SHA256SUMS.txt ! -name 99_RUN_LOG.txt -print0 \
            | sort -z | xargs -0 sha256sum >SHA256SUMS.txt
    )
fi

ARCHIVE_PATH="$OUTPUT_PARENT/${REPORT_NAME}.tar.gz"
if have tar; then
    tar -C "$OUTPUT_PARENT" -czf "$ARCHIVE_PATH" "$REPORT_NAME"
    echo "Готов архив: $ARCHIVE_PATH"
else
    echo "tar отсутствует; передайте каталог: $REPORT_DIR"
fi

echo
cat "$SUMMARY"
echo
echo "Диагностика завершена. Пришлите файл .tar.gz целиком."
exit 0
