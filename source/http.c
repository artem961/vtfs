#include <linux/err.h>
#include <linux/inet.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <asm/unaligned.h>

#include "http.h"

#define SERVER_IP "177.202.114.245"
#define SERVER_HOST "osi.arhr.tech"
#define API_TOKEN "vtfs-course-osi"
#define REQUEST_CAPACITY 4096
#define RESPONSE_CAPACITY 8192

static int append_value(char *request, size_t *length, const char *value)
{
    const unsigned char *p = (const unsigned char *)value;

    for (; *p; p++) {
        if (*length + 3 >= REQUEST_CAPACITY)
            return -ENAMETOOLONG;
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("-._~", *p))
            request[(*length)++] = *p;
        else
            *length += sprintf(request + *length, "%%%02X", *p);
    }
    request[*length] = '\0';
    return 0;
}

static int parse_response(char *raw, size_t received, char *response, size_t capacity)
{
    char *body = strstr(raw, "\r\n\r\n");
    char *line, *headers = raw;
    unsigned long content_length = 0;
    bool have_length = false;
    s64 code;
    size_t payload_length;

    if (!body || strncmp(raw, "HTTP/1.1 200 ", 13))
        return -EPROTO;
    *body = '\0';
    body += 4;
    while ((line = strsep(&headers, "\n"))) {
        size_t length = strlen(line);

        if (length && line[length - 1] == '\r')
            line[length - 1] = '\0';
        if (!strncasecmp(line, "Content-Length:", 15)) {
            if (have_length || kstrtoul(skip_spaces(line + 15), 10, &content_length))
                return -EPROTO;
            have_length = true;
        }
        if (!strncasecmp(line, "Transfer-Encoding:", 18))
            return -EPROTO;
    }
    if (!have_length || content_length < sizeof(u64) ||
        content_length != received - (body - raw))
        return -EPROTO;

    code = (s64)get_unaligned_le64(body);
    if (code)
        return code > 0 && code <= MAX_ERRNO ? -(int)code : -EPROTO;
    payload_length = content_length - sizeof(u64);
    if (!payload_length || body[content_length - 1] != '\0' ||
        memchr(body + sizeof(u64), '\0', payload_length - 1))
        return -EPROTO;
    if (payload_length > capacity)
        return -ENOSPC;
    memcpy(response, body + sizeof(u64), payload_length);
    return 0;
}

int vtfs_http_call(const char *method, char *response, size_t capacity,
                   size_t arg_count, ...)
{
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = in_aton(SERVER_IP),
        .sin_port = htons(80),
    };
    struct socket *sock;
    char *request, *raw;
    size_t length, sent = 0, received = 0, i;
    va_list args;
    int error;

    request = kmalloc(REQUEST_CAPACITY, GFP_NOFS);
    raw = kmalloc(RESPONSE_CAPACITY, GFP_NOFS);
    if (!request || !raw) {
        error = -ENOMEM;
        goto free_buffers;
    }
    length = snprintf(request, REQUEST_CAPACITY, "GET /api/%s?token=%s",
                      method, API_TOKEN);
    if (length >= REQUEST_CAPACITY) {
        error = -ENAMETOOLONG;
        goto free_buffers;
    }
    va_start(args, arg_count);
    error = 0;
    for (i = 0; i < arg_count && !error; i++) {
        const char *key = va_arg(args, const char *);
        const char *value = va_arg(args, const char *);
        int added = snprintf(request + length, REQUEST_CAPACITY - length,
                              "&%s=", key);

        if (added >= REQUEST_CAPACITY - length) {
            error = -ENAMETOOLONG;
            break;
        }
        length += added;
        error = append_value(request, &length, value);
    }
    va_end(args);
    if (error)
        goto free_buffers;
    error = snprintf(request + length, REQUEST_CAPACITY - length,
                     " HTTP/1.1\r\nHost: " SERVER_HOST "\r\nConnection: close\r\n\r\n");
    if (error >= REQUEST_CAPACITY - length) {
        error = -ENAMETOOLONG;
        goto free_buffers;
    }
    length += error;

    error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (error)
        goto free_buffers;
    sock->sk->sk_sndtimeo = 5 * HZ;
    sock->sk->sk_rcvtimeo = 5 * HZ;
    error = kernel_connect(sock, (struct sockaddr *)&address, sizeof(address), 0);
    if (error)
        goto close_socket;

    while (sent < length) {
        struct msghdr msg = { .msg_flags = MSG_NOSIGNAL };
        struct kvec vec = { .iov_base = request + sent, .iov_len = length - sent };

        error = kernel_sendmsg(sock, &msg, &vec, 1, vec.iov_len);
        if (error <= 0) {
            error = error ? error : -EIO;
            goto close_socket;
        }
        sent += error;
    }
    while (received < RESPONSE_CAPACITY - 1) {
        struct msghdr msg = {};
        struct kvec vec = {
            .iov_base = raw + received,
            .iov_len = RESPONSE_CAPACITY - 1 - received,
        };

        error = kernel_recvmsg(sock, &msg, &vec, 1, vec.iov_len, 0);
        if (error < 0)
            goto close_socket;
        if (!error)
            break;
        received += error;
    }
    raw[received] = '\0';
    error = received == RESPONSE_CAPACITY - 1 ? -EMSGSIZE :
            parse_response(raw, received, response, capacity);

close_socket:
    sock_release(sock);
free_buffers:
    kfree(raw);
    kfree(request);
    return error;
}
