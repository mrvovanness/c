#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

struct config {
    char file_path[4096];
    char socket_path[108];
    char pid_path[4096];
};

static int config_load(const char *path, struct config *cfg)
{
    FILE *fp = NULL;
    int rc = -1;
    char line[4200] = {0};

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "config: %s: %s\n", path, strerror(errno));
        goto out;
    }

    memset(cfg, 0, sizeof(*cfg));

    while (fgets(line, (int)sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            len = len - 1;
            line[len] = '\0';
        }
        if (len == 0 || line[0] == '#')
            continue;

        char *eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "config: malformed line: %s\n", line);
            goto out;
        }
        *eq = '\0';

        if (strcmp(line, "file") == 0)
            strncpy(cfg->file_path, eq + 1, sizeof(cfg->file_path) - 1);
        else if (strcmp(line, "socket") == 0)
            strncpy(cfg->socket_path, eq + 1, sizeof(cfg->socket_path) - 1);
        else if (strcmp(line, "pidfile") == 0)
            strncpy(cfg->pid_path, eq + 1, sizeof(cfg->pid_path) - 1);
    }

    if (!cfg->file_path[0] || !cfg->socket_path[0] || !cfg->pid_path[0]) {
        fprintf(stderr,
                "config: 'file', 'socket' and 'pidfile' keys required\n");
        goto out;
    }
    rc = 0;
out:
    if (fp)
        fclose(fp);
    return rc;
}

static int daemonize(void)
{
    pid_t pid = 0;
    int fd = -1;

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(EXIT_SUCCESS);

    if (setsid() < 0)
        return -1;

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(EXIT_SUCCESS);

    fd = open("/dev/null", O_RDWR);
    if (fd < 0)
        return -1;
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);

    return 0;
}

/* Open pidfile and take an exclusive flock; lock survives fork() into daemon. */
static int pidfile_acquire(const char *path)
{
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "pidfile: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK)
            fprintf(stderr,
                    "pidfile: %s: another instance is already running\n",
                    path);
        else
            fprintf(stderr, "pidfile: flock %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static int pidfile_write(int fd)
{
    char buf[32] = {0};
    int n = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
    if (n <= 0)
        return -1;
    if (ftruncate(fd, 0) < 0)
        return -1;
    if (lseek(fd, 0, SEEK_SET) < 0)
        return -1;
    if (write(fd, buf, (size_t)n) != (ssize_t)n)
        return -1;
    return 0;
}

static int serve(int listen_fd, const char *file_path)
{
    int client = -1;
    char buf[256] = {0};
    struct stat st = {0};
    int n = 0;

    client = accept(listen_fd, NULL, NULL);
    if (client < 0)
        return -1;

    if (stat(file_path, &st) < 0)
        n = snprintf(buf, sizeof(buf), "ERROR: %s\n", strerror(errno));
    else
        n = snprintf(buf, sizeof(buf), "%lld\n", (long long)st.st_size);

    if (n > 0)
        (void)write(client, buf, (size_t)n);
    close(client);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *conf = NULL;
    int fg = 0;
    int opt = 0;
    int rc = EXIT_FAILURE;
    int sock_fd = -1;
    int pid_fd = -1;
    int sock_bound = 0;
    struct config cfg = {0};
    struct sockaddr_un addr = {0};

    while ((opt = getopt(argc, argv, "fc:")) != -1) {
        switch (opt) {
        case 'c':
            conf = optarg;
            break;
        case 'f':
            fg = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-f] -c <config>\n", argv[0]);
            goto out;
        }
    }
    if (!conf) {
        fprintf(stderr, "Usage: %s [-f] -c <config>\n", argv[0]);
        goto out;
    }

    if (config_load(conf, &cfg) < 0)
        goto out;

    pid_fd = pidfile_acquire(cfg.pid_path);
    if (pid_fd < 0)
        goto out;

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, cfg.socket_path, sizeof(addr.sun_path) - 1);

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        goto out;
    }
    unlink(cfg.socket_path);
    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        goto out;
    }
    sock_bound = 1;
    if (listen(sock_fd, 1) < 0) {
        perror("listen");
        goto out;
    }

    if (!fg && daemonize() < 0) {
        perror("daemonize");
        goto out;
    }

    if (pidfile_write(pid_fd) < 0) {
        perror("pidfile write");
        goto out;
    }

    for (;;) {
        if (serve(sock_fd, cfg.file_path) < 0)
            break;
    }

    rc = EXIT_SUCCESS;
out:
    if (sock_fd >= 0)
        close(sock_fd);
    if (sock_bound)
        unlink(cfg.socket_path);
    if (pid_fd >= 0) {
        unlink(cfg.pid_path);
        close(pid_fd);
    }
    exit(rc);
}
