#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int config_load(const char *path, struct config *cfg)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "config: cannot open '%s': %s\n", path,
                strerror(errno));
        return -1;
    }

    memset(cfg, 0, sizeof(*cfg));

    char line[4200];
    int lineno = 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        ++lineno;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        if (len == 0 || line[0] == '#')
            continue;

        char *eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "config:%d: expected key=value\n", lineno);
            fclose(fp);
            return -1;
        }

        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "file") == 0) {
            if (strlen(val) >= sizeof(cfg->file_path)) {
                fprintf(stderr, "config:%d: file path too long\n", lineno);
                fclose(fp);
                return -1;
            }
            strncpy(cfg->file_path, val, sizeof(cfg->file_path) - 1);
        } else if (strcmp(key, "socket") == 0) {
            if (strlen(val) >= sizeof(cfg->socket_path)) {
                fprintf(stderr, "config:%d: socket path too long\n", lineno);
                fclose(fp);
                return -1;
            }
            strncpy(cfg->socket_path, val, sizeof(cfg->socket_path) - 1);
        } else {
            fprintf(stderr, "config:%d: unknown key '%s'\n", lineno, key);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    if (cfg->file_path[0] == '\0') {
        fprintf(stderr, "config: missing 'file' key\n");
        return -1;
    }
    if (cfg->socket_path[0] == '\0') {
        fprintf(stderr, "config: missing 'socket' key\n");
        return -1;
    }

    return 0;
}
