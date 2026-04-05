#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

struct config {
    char file_path[4096];
    char socket_path[108];
};

int config_load(const char *path, struct config *cfg);

#endif
