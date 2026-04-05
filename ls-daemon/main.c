#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

struct config {
    char file_path[4096];
    char socket_path[108];
};

static int config_load(const char *path, struct config *cfg)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "config: %s: %s\n", path, strerror(errno));
        return -1;
    }

    memset(cfg, 0, sizeof(*cfg));
    char line[4200];

    while (fgets(line, (int)sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';
        if (len == 0 || line[0] == '#')
            continue;

        char *eq = strchr(line, '=');
        if (!eq) {
            fclose(fp);
            return -1;
        }
        *eq = '\0';

        if (strcmp(line, "file") == 0)
            strncpy(cfg->file_path, eq + 1, sizeof(cfg->file_path) - 1);
        else if (strcmp(line, "socket") == 0)
            strncpy(cfg->socket_path, eq + 1, sizeof(cfg->socket_path) - 1);
    }
    fclose(fp);

    if (!cfg->file_path[0] || !cfg->socket_path[0]) {
        fprintf(stderr, "config: 'file' and 'socket' keys required\n");
        return -1;
    }
    return 0;
}

static int daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);

    if (setsid() < 0)
        return -1;

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0)
        return -1;
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);

    return 0;
}

static int serve(int listen_fd, const char *file_path)
{
    int client = accept(listen_fd, NULL, NULL);
    if (client < 0)
        return -1;

    char buf[256];
    struct stat st;
    int n;

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
    int fg = 0, opt;

    while ((opt = getopt(argc, argv, "fc:")) != -1) {
        switch (opt) {
        case 'c': conf = optarg; break;
        case 'f': fg = 1; break;
        default:
            fprintf(stderr, "Usage: %s [-f] -c <config>\n", argv[0]);
            return 1;
        }
    }
    if (!conf) {
        fprintf(stderr, "Usage: %s [-f] -c <config>\n", argv[0]);
        return 1;
    }

    struct config cfg;
    if (config_load(conf, &cfg) < 0)
        return 1;

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, cfg.socket_path, sizeof(addr.sun_path) - 1);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    unlink(cfg.socket_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 1) < 0) {
        perror("bind/listen");
        return 1;
    }

    if (!fg && daemonize() < 0) {
        perror("daemonize");
        return 1;
    }

    for (;;) {
        if (serve(fd, cfg.file_path) < 0)
            break;
    }

    close(fd);
    unlink(cfg.socket_path);
    return 0;
}
