#ifndef HTTP_H
#define HTTP_H

#include <time.h>
#include <sys/socket.h>
#include "stream.h"

// MacOSXなどではMSG_NOSIGNALは非対応。
#ifndef MSG_NOSIGNAL
#define NON_MSG_NOSIGNAL 1
#define MSG_NOSIGNAL 0
#endif

#define ERR_FATAL -1
#define ERR_PARSE -2

#define URI_LEN 512
#define PATH_LEN 1024

typedef enum _http_version
{
    HTTP_VER_UNKNOWN,
    HTTP_VER_1_0,
    HTTP_VER_1_1
} http_version;

typedef enum _http_connection
{
    CONNECTION_UNSPECIFIED,
    CONNECTION_UNKOWN,
    CONNECTION_KEEP_ALIVE,
    CONNECTION_CLOSE
} http_connection;

typedef enum _http_method
{
    METHOD_NOT_SUPPORTED,
    METHOD_GET,
    METHOD_HEAD,
} http_method;

typedef enum _http_status_code
{
    STATUS_NULL = 0,
    STATUS_CONTINUE = 100,
    STATUS_OK = 200,
    STATUS_BAD_REQUEST = 400,
    STATUS_FORBIDDEN = 403,
    STATUS_NOT_FOUND = 404,
    STATUS_METHOD_NOT_ALLOWED = 405,
    STATUS_REQUEST_URI_TOO_LONG = 412,
    STATUS_REQUEST_HEADER_FIELD_TOO_LARGE = 431,
    STATUS_INTERNAL_SERVER_ERROR = 500,
    STATUS_NOT_IMPLEMENTED = 501,
    STATUS_HTTP_VERSION_NOT_SUPPORTED = 505
} http_status_code;

typedef enum _mine_type
{
    MINE_TEXT_PLAIN,  // text/plain
    MINE_TEXT_HTML,  // text/html
    MINE_APPLICATION_XHTML_XML, // application/xhtml+xml
    MINE_IMAGE_GIF,   // image/gif
    MINE_IMAGE_JPEG,  // image/jpeg
    MINE_IMAGE_PNG,   // image/png
    MINE_VIDEO_MPEG,  // video/mpeg
    MINE_APPLICATION_PDF, // application/pdf
} mine_type;

typedef struct _http_request_head
{
    http_version version;
    http_connection connection;
    http_method method;
    char uri[URI_LEN];
    http_status_code parse_status;
} http_request_head;

typedef struct _http_response_head
{
    http_version version;
    http_connection connection;
    http_status_code status;
    struct tm date;
    off_t content_length;
    mine_type content_type;
    int content_fd;
} http_response_head;

int parse_request_head(stream_t*, http_request_head*);
int make_response_head(http_request_head*, http_response_head*, char*);
int send_response_head(int socket, http_response_head *res);
int send_response_body(int socket, http_response_head *res);
int discard_response(http_response_head *res);

#endif
