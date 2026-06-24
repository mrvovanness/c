/*
 * syscall_demo.c — небольшая демонстрационная программа для трассировки ftrace.
 *
 * Программа намеренно выполняет детерминированную последовательность из
 * 11 РАЗЛИЧНЫХ системных вызовов, чтобы их было удобно наблюдать в ftrace:
 *
 *      1)  getpid()      — получить PID процесса
 *      2)  uname()       — информация о ядре/системе
 *      3)  openat()      — создать/открыть файл (libc open() -> openat)
 *      4)  write()       — записать данные в файл
 *      5)  fsync()       — сбросить буферы файла на диск
 *      6)  lseek()       — переместить смещение в начало файла
 *      7)  read()        — прочитать данные обратно
 *      8)  close()       — закрыть файловый дескриптор
 *      9)  newfstatat()  — получить метаданные файла (libc stat())
 *      10) unlink()      — удалить файл
 *      11) nanosleep()   — короткая пауза (заметный по времени маркер в трассе)
 *
 * Дополнительно при старте/завершении процесса ядро выполняет служебные
 * вызовы (execve, brk, mmap, set_tid_address, exit_group и т.п.), которые
 * также видны в трассировке и разбираются в отчёте.
 *
 * Сборка:   make            (см. Makefile, статическая сборка)
 * Запуск:   ./syscall_demo
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>

#define DEMO_FILE "/tmp/ftrace_demo.txt"
#define DEMO_MSG  "ftrace syscall demo: hello, kernel!\n"

/* Печать ошибки и выход; perror использует свой системный вызов write(2). */
static void die(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

int main(void)
{
    /* 1) getpid(): идентификатор текущего процесса. */
    pid_t pid = getpid();
    printf("[demo] pid = %d\n", (int)pid);

    /* 2) uname(): сведения о ядре. */
    struct utsname uts;
    if (uname(&uts) == -1)
        die("uname");
    printf("[demo] kernel = %s %s (%s)\n", uts.sysname, uts.release, uts.machine);

    /* 3) openat(): создаём файл на запись/чтение. */
    int fd = open(DEMO_FILE, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd == -1)
        die("open");

    /* 4) write(): записываем строку. */
    size_t msg_len = strlen(DEMO_MSG);
    if (write(fd, DEMO_MSG, msg_len) != (ssize_t)msg_len)
        die("write");

    /* 5) fsync(): гарантированно сбрасываем данные на носитель. */
    if (fsync(fd) == -1)
        die("fsync");

    /* 6) lseek(): возвращаемся в начало файла перед чтением. */
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
        die("lseek");

    /* 7) read(): читаем записанное обратно. */
    char buf[128];
    ssize_t nread = read(fd, buf, sizeof(buf) - 1);
    if (nread == -1)
        die("read");
    buf[nread] = '\0';

    /* 8) close(): закрываем дескриптор. */
    if (close(fd) == -1)
        die("close");

    /* 9) stat() -> newfstatat(): метаданные файла. */
    struct stat st;
    if (stat(DEMO_FILE, &st) == -1)
        die("stat");
    printf("[demo] file size = %lld bytes\n", (long long)st.st_size);

    /* 10) unlink(): удаляем временный файл. */
    if (unlink(DEMO_FILE) == -1)
        die("unlink");

    /* 11) nanosleep(): пауза 20 мс — хорошо заметна в таймингах ftrace. */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 20L * 1000 * 1000 };
    if (nanosleep(&ts, NULL) == -1)
        die("nanosleep");

    printf("[demo] done: прочитано %zd байт, содержимое: %s", nread, buf);
    return 0;
}
