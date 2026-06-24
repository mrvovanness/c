#!/usr/bin/env bash
#
# run_demo.sh — полный воспроизводимый прогон в выделенной Linux‑VM.
#
# Зачем: на хосте macOS нет Linux‑ядра с ftrace. Скрипт поднимает ОТДЕЛЬНУЮ
# одноразовую VM (Fedora CoreOS, ftrace включён) через `podman machine`,
# собирает в ней программу, прогоняет ftrace и забирает результаты — не
# затрагивая основную podman‑машину пользователя.
#
# Шаги:
#   1. создать и запустить VM `ftrace-vm`;
#   2. скопировать в неё исходники (по ssh, в обход подключения podman);
#   3. собрать статический бинарник в контейнере alpine (gcc + musl);
#   4. запустить trace.sh от root внутри VM — там настоящий tracefs;
#   5. выгрузить каталог results/ обратно на хост;
#   6. (опционально) удалить VM.
#
# Запуск:  ./run_demo.sh
set -euo pipefail

VM=ftrace-vm
HERE=$(cd "$(dirname "$0")" && pwd)
KNOWN_HOSTS=$(mktemp)
trap 'rm -f "$KNOWN_HOSTS"' EXIT

# 1. VM
podman machine inspect "$VM" >/dev/null 2>&1 || \
    podman machine init --cpus 2 --memory 2048 --disk-size 20 "$VM"
podman machine start "$VM" 2>/dev/null || true

# Параметры ssh берём из самого podman (порт/ключ/пользователь динамические).
PORT=$(podman machine inspect --format '{{.SSHConfig.Port}}' "$VM")
KEY=$(podman machine inspect --format '{{.SSHConfig.IdentityPath}}' "$VM")
SSH=(ssh -F /dev/null -i "$KEY" -p "$PORT"
     -o UserKnownHostsFile="$KNOWN_HOSTS" -o StrictHostKeyChecking=no
     -o IdentitiesOnly=yes -o LogLevel=ERROR root@127.0.0.1)

# 2. Копируем исходники в VM.
"${SSH[@]}" 'rm -rf /root/ftrace && mkdir -p /root/ftrace'
tar -C "$HERE" -cf - syscall_demo.c Makefile trace.sh analyze.sh \
    | "${SSH[@]}" 'tar -C /root/ftrace -xf -'

# 3. Сборка статического бинарника в контейнере (в CoreOS своего gcc нет).
"${SSH[@]}" '
    podman run --rm -v /root/ftrace:/work:Z -w /work docker.io/library/alpine:latest \
        sh -c "apk add --no-cache gcc musl-dev make >/dev/null && make CC=gcc" '

# 4. Настройка ftrace и сбор данных — от root, прямо в VM (там есть tracefs).
"${SSH[@]}" 'cd /root/ftrace && bash trace.sh ./syscall_demo results && \
             bash analyze.sh results/10_syscalls.trace > results/30_syscall_summary.txt'

# 5. Забираем результаты на хост.
rm -rf "$HERE/results"
"${SSH[@]}" 'tar -C /root/ftrace -cf - results' | tar -C "$HERE" -xf -

echo "Готово. Реальные данные ftrace — в $HERE/results/"
echo "Удалить VM:  podman machine rm -f $VM"
