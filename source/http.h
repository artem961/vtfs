#ifndef VTFS_HTTP_H
#define VTFS_HTTP_H

#include <linux/types.h>

/* Аргументы передаются парами: имя параметра, значение. */
int vtfs_http_call(const char *method, char *response, size_t capacity,
                   size_t arg_count, ...);

#endif
