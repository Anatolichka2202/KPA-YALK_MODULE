#!/usr/bin/env bash
set -u

section() {
    printf '\n==== %s ====\n' "$1"
}

have() {
    command -v "$1" >/dev/null 2>&1
}

section "ОС"
if [[ -r /etc/os-release ]]; then
    cat /etc/os-release
else
    echo "/etc/os-release недоступен"
fi
printf 'kernel: %s\n' "$(uname -r)"
printf 'machine: %s\n' "$(uname -m)"

section "Компилятор и сборка"
for tool in gcc g++ cmake ninja make qmake6 qtpaths6 pkg-config; do
    if have "$tool"; then
        printf '%-12s ' "$tool"
        "$tool" --version 2>&1 | head -n 1 || true
    else
        printf '%-12s отсутствует\n' "$tool"
    fi
done

section "Заголовки ядра"
KBUILD="/lib/modules/$(uname -r)/build"
if [[ -d "$KBUILD" ]]; then
    echo "OK: $KBUILD"
else
    echo "НЕТ: $KBUILD"
    echo "Без заголовков текущего ядра DKMS не соберёт модуль E20-10."
fi

section "Пакеты LComp"
if have dpkg-query; then
    for pkg in lcomp-dkms liblcomp1 liblcomp1-dev; do
        if dpkg-query -W -f='${Status} ${Version}\n' "$pkg" 2>/dev/null | grep -q '^install ok installed '; then
            printf '%-16s ' "$pkg"
            dpkg-query -W -f='${Version}\n' "$pkg" 2>/dev/null || true
        else
            printf '%-16s не установлен\n' "$pkg"
        fi
    done
else
    echo "dpkg-query отсутствует"
fi

section "DKMS"
if have dkms; then
    dkms status 2>/dev/null || true
else
    echo "dkms отсутствует"
fi

section "Загруженные модули"
lsmod 2>/dev/null | grep -Ei 'lcomp|lcard|e2010' || echo "Модуль LComp/E20-10 не найден"

section "Библиотека LComp"
if have ldconfig; then
    ldconfig -p 2>/dev/null | grep -i lcomp || echo "liblcomp отсутствует в кеше ldconfig"
fi

section "USB"
if have lsusb; then
    lsusb
else
    echo "lsusb отсутствует (пакет usbutils)"
fi

section "Сообщения ядра"
if have journalctl; then
    journalctl -k -n 120 --no-pager 2>/dev/null | grep -Ei 'usb|lcomp|lcard|e20|e2010' | tail -n 80 || true
elif have dmesg; then
    dmesg 2>/dev/null | grep -Ei 'usb|lcomp|lcard|e20|e2010' | tail -n 80 || true
fi

section "Устройства"
find /dev -maxdepth 2 \
    \( -iname '*lcomp*' -o -iname '*lcard*' -o -iname '*e2010*' -o -iname '*e20*' \) \
    -print 2>/dev/null | head -n 80 || true

section "Итог"
echo "Запускайте при подключённом E20-10:"
echo "bash scripts/diag_e2010_astra.sh | tee e2010_astra_diag.txt"
