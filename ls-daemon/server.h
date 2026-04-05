#ifndef SERVER_H
#define SERVER_H

#include "config.h"

int server_listen(const struct config *cfg);
int server_handle_client(int listen_fd, const struct config *cfg);

#endif
