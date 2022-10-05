#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

#define VERBOSE 1

typedef struct {
    uv_fs_t req;
    uv_buf_t buf;
} fs_context_t;

typedef struct {
    uv_write_t req;
    uv_stream_t *stream;
    uv_buf_t buf;
} sock_context_t;

typedef struct {
    fs_context_t stdin_ctx;
    fs_context_t stdout_ctx;
    sock_context_t sock_ctx;
} app_context_t;

typedef struct {
    app_context_t *app;
    const char *host;
    const char *port;
} getaddrinfo_context_t;

typedef struct {
    app_context_t *app;
    char addr[17];
    const char *port;
} connect_context_t;

void print_err(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fflush(stderr);
}

void print_usage(char *exec) {
    print_err("Usage: %s <port> <host> [...hosts]\n", basename(exec));
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    *buf = uv_buf_init((char *)malloc(suggested_size), suggested_size);
}

void read_sock_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    app_context_t *app = (app_context_t *)stream->data;
    if (nread <= 0) {
        uv_fs_close(stream->loop, (uv_fs_t *)&app->stdin_ctx, STDIN_FILENO, NULL);
        goto cleanup;
    }

    uv_buf_t *wbuf = &app->stdout_ctx.buf;
    memcpy(wbuf->base, buf->base, nread);
    wbuf->len = nread;
    uv_fs_write(stream->loop, (uv_fs_t *)&app->stdout_ctx, STDOUT_FILENO, wbuf, 1, -1, NULL);

cleanup:
    if (buf) free(buf->base);
}

void read_stdin_cb(uv_fs_t *req) {
    app_context_t *app = (app_context_t *)req->data;
    sock_context_t *sock_ctx = &app->sock_ctx;
    fs_context_t *stdin_ctx = &app->stdin_ctx;
    if (req->result <= 0) {
        uv_read_stop(sock_ctx->stream);
        return;
    }

    uv_buf_t *rbuf = &stdin_ctx->buf;
    uv_buf_t *sbuf = &sock_ctx->buf;
    memcpy(sbuf->base, rbuf->base, req->result);
    sbuf->len = req->result;

    uv_write((uv_write_t *)sock_ctx, sock_ctx->stream, sbuf, 1, NULL);
    uv_fs_read(req->loop, req, STDIN_FILENO, rbuf, 1, -1, read_stdin_cb);
}

void connect_cb(uv_connect_t *req, int status) {
    uv_stream_t *stream = req->handle;
    connect_context_t *ctx = (connect_context_t *)req->data;
    app_context_t *app = ctx->app;
    sock_context_t *sock_ctx = &app->sock_ctx;

    if (sock_ctx->stream) {
        goto cleanup;
    } else if (status) {
        print_err("Failed to connect to %s: %s\n", ctx->addr, uv_strerror(status));
        goto cleanup;
    }
    sock_ctx->stream = stream;
    print_err("Connected to %s\n", ctx->addr);

    stream->data = app;
    uv_read_start(stream, alloc_buffer, read_sock_cb);
    uv_fs_read(stream->loop, (uv_fs_t *)&app->stdin_ctx, STDIN_FILENO, &app->stdin_ctx.buf, 1, -1, read_stdin_cb);

cleanup:
    if (sock_ctx->stream != stream) free(stream);
    free(ctx);
    free(req);
}

void resolved_cb(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res) {
    getaddrinfo_context_t *ctx = (getaddrinfo_context_t *)resolver->data;
    if (status) {
        print_err("Failed to resolve address %s: %s\n", ctx->host, uv_strerror(status));
        goto cleanup;
    }

    connect_context_t *con_ctx = (connect_context_t *)malloc(sizeof(connect_context_t));
    con_ctx->app = ctx->app;
    con_ctx->port = ctx->port;
    uv_ip4_name((struct sockaddr_in *)res->ai_addr, con_ctx->addr, sizeof(con_ctx->addr));

    uv_connect_t *req = (uv_connect_t *)malloc(sizeof(uv_connect_t));
    req->data = con_ctx;

    uv_tcp_t *socket = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(resolver->loop, socket);
    uv_tcp_connect(req, socket, res->ai_addr, connect_cb);

cleanup:
    free(ctx);
    free(resolver);
    if (res) uv_freeaddrinfo(res);
}

int main(int argc, char *argv[]) {
    int rc;
    char rbuf[512], wbuf[512], sbuf[512];
    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    app_context_t app = {0};
    app.stdin_ctx.buf = uv_buf_init(rbuf, sizeof(rbuf));
    app.stdin_ctx.req.data = &app;
    app.stdout_ctx.buf = uv_buf_init(wbuf, sizeof(wbuf));
    app.stdout_ctx.req.data = &app;
    app.sock_ctx.buf = uv_buf_init(sbuf, sizeof(sbuf));
    app.sock_ctx.req.data = &app;

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    uv_loop_t *loop = uv_default_loop();
    const char *port = argv[1];
    for (int i = 2; i < argc; i++) {
        const char *host = argv[i];

        getaddrinfo_context_t *data = (getaddrinfo_context_t *)malloc(sizeof(getaddrinfo_context_t));
        data->app = &app;
        data->host = host;
        data->port = port;

        uv_getaddrinfo_t *req = (uv_getaddrinfo_t *)malloc(sizeof(uv_getaddrinfo_t));
        req->data = data;

        rc = uv_getaddrinfo(loop, req, resolved_cb, host, port, &hints);
        if (rc) print_err("Failed to resolve host %s: %s\n", host, uv_err_name(rc));
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}
