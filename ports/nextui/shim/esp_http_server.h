#pragma once
// Declarations only: the companion HTTP server is not built for Linux hosts (see ports/nextui/stubs).
#include <cstddef>
#include "esp_err.h"

typedef void* httpd_handle_t;
typedef enum { HTTP_DELETE = 0, HTTP_GET = 1, HTTP_HEAD = 2, HTTP_POST = 3, HTTP_PUT = 4, HTTP_OPTIONS = 6, HTTP_PATCH = 28 } httpd_method_t;
typedef enum {
    HTTPD_500_INTERNAL_SERVER_ERROR = 0, HTTPD_501_METHOD_NOT_IMPLEMENTED, HTTPD_505_VERSION_NOT_SUPPORTED,
    HTTPD_400_BAD_REQUEST, HTTPD_401_UNAUTHORIZED, HTTPD_403_FORBIDDEN, HTTPD_404_NOT_FOUND,
    HTTPD_405_METHOD_NOT_ALLOWED, HTTPD_408_REQ_TIMEOUT, HTTPD_411_LENGTH_REQUIRED, HTTPD_414_URI_TOO_LONG,
    HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE,
} httpd_err_code_t;
typedef struct httpd_req {
    httpd_handle_t handle;
    int method;
    const char uri[513];
    size_t content_len;
    void* aux;
    void* user_ctx;
    void* sess_ctx;
    void (*free_ctx)(void*);
    bool ignore_sess_ctx_changes;
} httpd_req_t;

void* httpd_get_global_user_ctx(httpd_handle_t handle);
esp_err_t httpd_resp_set_hdr(httpd_req_t* request, const char* field, const char* value);
