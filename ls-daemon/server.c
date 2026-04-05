#include "server.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int server_listen(const struct config *cfg)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, cfg->socket_path, sizeof(addr.sun_path) - 1);

    unlink(cfg->socket_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 1) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

int server_handle_client(int listen_fd, const struct config *cfg)
{
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno == EINTR)
            return 0;
        perror("accept");
        return -1;
    }

    char buf[256];
    struct stat st;

    if (stat(cfg->file_path, &st) < 0) {
        int n = snprintf(buf, sizeof(buf), "ERROR: %s: %s\n", cfg->file_path,
                         strerror(errno));
        if (n > 0)
            (void)write(client_fd, buf, (size_t)n);
    } else {
        int n = snprintf(buf, sizeof(buf), "%lld\n", (long long)st.st_size);
        if (n > 0)
            (void)write(client_fd, buf, (size_t)n);
    }

    close(client_fd);
    return 0;
}
