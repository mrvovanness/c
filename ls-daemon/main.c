#include "config.h"
#include "daemon.h"
#include "server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-f] -c <config>\n", prog);
    fprintf(stderr, "  -c <config>  path to configuration file\n");
    fprintf(stderr, "  -f           run in foreground (no daemonization)\n");
}

int main(int argc, char *argv[])
{
    const char *config_path = NULL;
    int foreground = 0;
    int opt;

    while ((opt = getopt(argc, argv, "fc:")) != -1) {
        switch (opt) {
        case 'c':
            config_path = optarg;
            break;
        case 'f':
            foreground = 1;
            break;
        default:
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!config_path) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    struct config cfg;
    if (config_load(config_path, &cfg) < 0)
        return EXIT_FAILURE;

    int listen_fd = server_listen(&cfg);
    if (listen_fd < 0)
        return EXIT_FAILURE;

    if (!foreground) {
        if (daemonize() < 0) {
            close(listen_fd);
            unlink(cfg.socket_path);
            return EXIT_FAILURE;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    if (foreground)
        fprintf(stderr, "ls-daemon: listening on %s\n", cfg.socket_path);

    while (g_running) {
        if (server_handle_client(listen_fd, &cfg) < 0)
            break;
    }

    close(listen_fd);
    unlink(cfg.socket_path);

    return EXIT_SUCCESS;
}
