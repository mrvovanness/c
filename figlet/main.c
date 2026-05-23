#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define HOST "telehack.com"
#define PORT "23"
#define READ_BUF_SIZE 4096
#define IO_TIMEOUT_SEC 60

/* Telnet protocol constants (RFC 854) */
#define TELNET_SE 240
#define TELNET_SB 250
#define TELNET_WILL 251
#define TELNET_WONT 252
#define TELNET_DO 253
#define TELNET_DONT 254
#define TELNET_IAC 255

static int connect_to_host(const char* host, const char* port) {
    struct addrinfo hints = {0};
    struct addrinfo* result = NULL;
    int sock = -1;
    int last_errno = 0;
    int err;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    err = getaddrinfo(host, port, &hints, &result);
    if (err != 0) {
        fprintf(stderr, "Error: cannot resolve %s: %s\n", host,
                gai_strerror(err));
        return -1;
    }

    for (struct addrinfo* rp = result; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) {
            last_errno = errno;
            continue;
        }
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        last_errno = errno;
        close(sock);
        sock = -1;
    }

    freeaddrinfo(result);

    if (sock < 0) {
        fprintf(stderr, "Error: cannot connect to %s:%s: %s\n", host, port,
                strerror(last_errno));
        return -1;
    }

    struct timeval tv = {.tv_sec = IO_TIMEOUT_SEC, .tv_usec = 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0 ||
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        fprintf(stderr, "Error: setsockopt failed: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

static int write_all(int fd, const void* buf, size_t len) {
    const unsigned char* p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Telnet stream parser: refuses every option negotiation and strips IAC
 * sequences from the byte stream. */
struct telnet_parser {
    enum {
        TP_DATA,
        TP_IAC,
        TP_OPT,
        TP_SB,
        TP_SB_IAC,
    } state;
    unsigned char cmd;
    int sock;
};

/* Feed one byte to the parser. Returns:
 *   1 — a regular data byte was produced (in *out)
 *   0 — byte was consumed by protocol handling
 *  -1 — I/O error while replying to the server */
static int telnet_feed(struct telnet_parser* p, unsigned char in,
                       unsigned char* out) {
    switch (p->state) {
        case TP_DATA:
            if (in == TELNET_IAC) {
                p->state = TP_IAC;
                return 0;
            }
            *out = in;
            return 1;

        case TP_IAC:
            if (in == TELNET_IAC) {
                /* IAC IAC is an escaped 0xFF data byte. */
                p->state = TP_DATA;
                *out = TELNET_IAC;
                return 1;
            }
            if (in == TELNET_WILL || in == TELNET_WONT || in == TELNET_DO ||
                in == TELNET_DONT) {
                p->cmd = in;
                p->state = TP_OPT;
                return 0;
            }
            if (in == TELNET_SB) {
                p->state = TP_SB;
                return 0;
            }
            /* Other 2-byte commands (NOP, AYT, ...) — ignore. */
            p->state = TP_DATA;
            return 0;

        case TP_OPT:
            /* Refuse every option. WONT/DONT need no reply since we never
             * agreed to anything. */
            if (p->cmd == TELNET_WILL || p->cmd == TELNET_DO) {
                unsigned char reply[3];
                reply[0] = TELNET_IAC;
                reply[1] = (p->cmd == TELNET_WILL) ? TELNET_DONT : TELNET_WONT;
                reply[2] = in;
                if (write_all(p->sock, reply, 3) < 0) return -1;
            }
            p->state = TP_DATA;
            return 0;

        case TP_SB:
            if (in == TELNET_IAC) p->state = TP_SB_IAC;
            return 0;

        case TP_SB_IAC:
            p->state = (in == TELNET_SE) ? TP_DATA : TP_SB;
            return 0;
    }
    return 0;
}

struct buffer {
    char* data;
    size_t size;
    size_t cap;
};

static int buffer_append(struct buffer* b, char c) {
    if (b->size + 1 > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 4096;
        char* p = realloc(b->data, new_cap);
        if (!p) return -1;
        b->data = p;
        b->cap = new_cap;
    }
    b->data[b->size++] = c;
    return 0;
}

/* Read filtered bytes from sock, appending to out. If needle is non-NULL,
 * stop as soon as it appears in the filtered stream (and treat early EOF
 * as an error). If needle is NULL, read until the server closes. */
static int read_stream(int sock, struct telnet_parser* parser,
                       struct buffer* out, const char* needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    size_t matched = 0;
    unsigned char chunk[READ_BUF_SIZE];

    for (;;) {
        ssize_t n = read(sock, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "Error: read from server failed: %s\n",
                    strerror(errno));
            return -1;
        }
        if (n == 0) {
            if (needle && matched < needle_len) {
                fprintf(stderr,
                        "Error: server closed connection "
                        "before expected response\n");
                return -1;
            }
            return 0;
        }

        for (ssize_t i = 0; i < n; i++) {
            unsigned char c;
            int r = telnet_feed(parser, chunk[i], &c);
            if (r < 0) {
                fprintf(stderr, "Error: write to server failed: %s\n",
                        strerror(errno));
                return -1;
            }
            if (r == 0) continue;
            if (buffer_append(out, (char)c) < 0) {
                fprintf(stderr, "Error: out of memory\n");
                return -1;
            }
            if (!needle) continue;

            /* Substring matching with naive restart — fine for short
             * needles without internal repetition. */
            if ((char)c == needle[matched]) {
                matched++;
                if (matched == needle_len) return 0;
            } else {
                matched = ((char)c == needle[0]) ? 1 : 0;
            }
        }
    }
}

/* Locate the rendered figlet body inside the captured session.
 * On success returns a slice [*body, *body + *len) into buf->data. */
static int extract_body(const struct buffer* buf, const char** body,
                        size_t* len) {
    /* The session is bracketed by two prompts ("." on its own line):
     *   <banner>\r\n.<command response>\r\n.<closing>
     * The figlet output sits between the first and second prompt. If the
     * server echoes input back to us, the response also begins with the
     * echoed command line, which we skip. */
    const char* first = strstr(buf->data, "\n.");
    if (first == NULL) {
        fprintf(stderr, "Error: no prompt in server response\n");
        return -1;
    }
    const char* start = first + 2; /* skip "\n." */
    const char* end = buf->data + buf->size;

    const char* second = strstr(start, "\n.");
    if (second != NULL) {
        end = second + 1; /* keep the \n that ends the last art line */
    }

    /* Skip a leading command-echo line, if present. */
    size_t remaining = (size_t)(end - start);
    if (remaining >= 7 && memcmp(start, "figlet ", 7) == 0) {
        const char* nl = memchr(start, '\n', remaining);
        if (nl != NULL && nl < end) start = nl + 1;
    }

    if (end < start) end = start;
    *body = start;
    *len = (size_t)(end - start);
    return 0;
}

int main(int argc, char* argv[]) {
    const char* font;
    const char* text;
    if (argc == 2) {
        font = "";
        text = argv[1];
    } else if (argc == 3) {
        font = argv[1];
        text = argv[2];
    } else {
        fprintf(stderr, "Usage: %s [<font>] <text>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (text[0] == '\0') {
        fprintf(stderr, "Error: text must not be empty\n");
        return EXIT_FAILURE;
    }
    if (strpbrk(text, "\r\n") != NULL) {
        fprintf(stderr, "Error: text must not contain line breaks\n");
        return EXIT_FAILURE;
    }
    if (strpbrk(font, "\r\n /") != NULL) {
        fprintf(stderr,
                "Error: font name must not contain whitespace, "
                "slashes, or line breaks\n");
        return EXIT_FAILURE;
    }

    /* telehack figlet syntax:
     *   figlet /<font> <text>   — render with the given font
     *   figlet <text>           — render with the default font
     * Do not pipeline anything else after the newline: telehack closes
     * the session on a queued "exit" before the figlet output is fully
     * flushed. We instead read until the next prompt and close from our
     * side. */
    char cmd[2048];
    int cmd_len;
    if (font[0] == '\0') {
        cmd_len = snprintf(cmd, sizeof(cmd), "figlet %s\r\n", text);
    } else {
        cmd_len = snprintf(cmd, sizeof(cmd), "figlet /%s %s\r\n", font, text);
    }
    if (cmd_len < 0 || (size_t)cmd_len >= sizeof(cmd)) {
        fprintf(stderr, "Error: command line too long\n");
        return EXIT_FAILURE;
    }

    /* A half-closed socket would otherwise raise SIGPIPE during write(). */
    signal(SIGPIPE, SIG_IGN);

    int sock = connect_to_host(HOST, PORT);
    if (sock < 0) return EXIT_FAILURE;

    int ret = EXIT_FAILURE;
    struct buffer buf = {0};
    struct telnet_parser parser = {.state = TP_DATA, .sock = sock};

    /* telehack ignores anything typed during the slow banner stream, so we
     * have to wait for the initial prompt ("." on its own line) before
     * sending the command. */
    if (read_stream(sock, &parser, &buf, "\n.") < 0) goto cleanup;

    if (write_all(sock, cmd, (size_t)cmd_len) < 0) {
        fprintf(stderr, "Error: cannot send command: %s\n", strerror(errno));
        goto cleanup;
    }

    /* Read until the next prompt — that marks the end of the figlet
     * output. */
    if (read_stream(sock, &parser, &buf, "\n.") < 0) goto cleanup;

    /* NUL-terminate so we can use string functions. */
    if (buffer_append(&buf, '\0') < 0) {
        fprintf(stderr, "Error: out of memory\n");
        goto cleanup;
    }

    const char* body;
    size_t body_len;
    if (extract_body(&buf, &body, &body_len) < 0) goto cleanup;

    if (body_len > 0 && fwrite(body, 1, body_len, stdout) != body_len) {
        fprintf(stderr, "Error: write to stdout failed\n");
        goto cleanup;
    }

    ret = EXIT_SUCCESS;

cleanup:
    free(buf.data);
    close(sock);
    return ret;
}
