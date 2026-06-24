#!/usr/bin/env bash
#
# trace.sh — настройка ftrace и сбор данных о системных вызовах программы.
#
# Скрипт использует «сырой» интерфейс ftrace через tracefs (без trace-cmd),
# чтобы наглядно показать, как ftrace настраивается вручную. Выполняет два
# независимых сеанса трассировки одной и той же программы:
#
#   1) События syscalls (sys_enter_*/sys_exit_*) — что за вызовы, с какими
#      аргументами и какой код возврата. Это основной материал для анализа.
#
#   2) Трассировщик function_graph — граф вызовов функций ВНУТРИ ядра при
#      обработке системных вызовов программы, с таймингами длительности.
#
# В обоих сеансах трассировка ОГРАНИЧЕНА нашим процессом через set_event_pid
# и set_ftrace_pid, поэтому в выводе нет «постороннего» системного шума.
#
# Запуск (от root, на Linux с включённым ftrace):
#     sudo ./trace.sh [путь_к_программе] [каталог_результатов]
#
# По умолчанию: ./syscall_demo и каталог results/
set -euo pipefail

PROG=${1:-./syscall_demo}
OUTDIR=${2:-results}

# --- Найти точку монтирования tracefs (в разных дистрибутивах путь разный) ---
TR=/sys/kernel/tracing
if [ ! -d "$TR/events" ]; then
    # современный путь
    mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null || true
fi
if [ ! -d "$TR/events" ] && [ -d /sys/kernel/debug ]; then
    # устаревший путь через debugfs
    mount -t debugfs nodev /sys/kernel/debug 2>/dev/null || true
    TR=/sys/kernel/debug/tracing
fi
if [ ! -d "$TR/events/syscalls" ]; then
    echo "ОШИБКА: ftrace/tracefs недоступен (нет $TR/events/syscalls)." >&2
    echo "Нужно ядро с CONFIG_FTRACE=y, CONFIG_FTRACE_SYSCALLS=y и права root." >&2
    exit 1
fi

PROG=$(readlink -f "$PROG")
mkdir -p "$OUTDIR"
OUTDIR=$(readlink -f "$OUTDIR")

echo ">> tracefs: $TR"
echo ">> программа: $PROG"
echo ">> результаты: $OUTDIR"

# --- Привести ftrace в исходное (выключенное) состояние ---
reset_tracer() {
    echo 0    > "$TR/tracing_on"
    echo nop  > "$TR/current_tracer"
    echo      > "$TR/set_event_pid"   2>/dev/null || true
    echo      > "$TR/set_ftrace_pid"  2>/dev/null || true
    echo 0    > "$TR/events/syscalls/enable"
    : > "$TR/trace"
}

# --- 0. Сохранить конфигурацию окружения (доказательство «как настроено») ---
{
    echo "### uname -a"
    uname -a
    echo
    echo "### tracefs mountpoint: $TR"
    echo
    echo "### available_tracers"
    cat "$TR/available_tracers"
    echo
    echo "### current_tracer (по умолчанию)"
    cat "$TR/current_tracer"
    echo
    echo "### tracing_on"
    cat "$TR/tracing_on"
    echo
    echo "### число доступных событий syscalls"
    ls "$TR/events/syscalls" | grep -c '^sys_enter_'
} > "$OUTDIR/00_env.txt"

reset_tracer

#############################################################################
# Сеанс 1. События системных вызовов (syscalls: sys_enter_* / sys_exit_*)
#############################################################################
echo ">> сеанс 1: события syscalls"
echo nop > "$TR/current_tracer"
echo 1   > "$TR/events/syscalls/enable"   # включить все sys_enter/sys_exit

# Дочерний процесс: регистрирует свой PID как фильтр событий, включает
# трассировку и заменяет себя нашей программой через exec. За счёт того, что
# exec сохраняет PID, в трассу попадают ТОЛЬКО события нашей программы.
bash -c "echo \$\$ > '$TR/set_event_pid'; echo 1 > '$TR/tracing_on'; exec '$PROG'" \
    > "$OUTDIR/prog_stdout.txt" 2>&1

echo 0 > "$TR/tracing_on"
cp "$TR/trace" "$OUTDIR/10_syscalls.trace"
reset_tracer

#############################################################################
# Сеанс 2. function_graph — граф вызовов внутри ядра с длительностями
#############################################################################
echo ">> сеанс 2: function_graph"
echo function_graph > "$TR/current_tracer"
# Показывать имя/PID процесса и длительность функций.
echo 1 > "$TR/options/funcgraph-proc"     2>/dev/null || true
echo 1 > "$TR/options/funcgraph-abstime"  2>/dev/null || true

bash -c "echo \$\$ > '$TR/set_ftrace_pid'; echo 1 > '$TR/tracing_on'; exec '$PROG'" \
    > /dev/null 2>&1

echo 0 > "$TR/tracing_on"
cp "$TR/trace" "$OUTDIR/20_function_graph.trace"
reset_tracer

echo ">> готово. Файлы в $OUTDIR:"
ls -l "$OUTDIR"
