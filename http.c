#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>

#include "http.h"
#include "stream.h"

// エラー処理
#define TRY_CATCH_INIT int TRY_TMP;
#define TRY_IF(x) if ((TRY_TMP=(x))==-1) { goto CATCH_FATAL; } else if (TRY_TMP)
#define TRY_ELIF(x) else if ((TRY_TMP=(x))==-1) { goto CATCH_FATAL; } else if (TRY_TMP)

/* ヘッダをパース */
int parse_request_head(stream_t* buff, http_request_head* out)
{
    const int name_len = 256;
    char name[name_len];
    TRY_CATCH_INIT;

    out->parse_status = STATUS_NULL;

    // メソッド
    TRY_IF(stream_take(buff, "GET", INSENSITIVE)) {
        printf("method: GET\n");
        out->method = METHOD_GET;
    } TRY_ELIF((stream_take(buff, "HEAD", INSENSITIVE))) {
        printf("method: HEAD\n");
        out->method = METHOD_HEAD;
    } else {
        out->method = METHOD_NOT_SUPPORTED;
        out->parse_status = STATUS_NOT_IMPLEMENTED;
        goto CATCH_PARSE;
    }

    // URLパース
    TRY_IF(stream_take_token(buff, out->uri, URI_LEN)) {
        printf("uri: %s\n", out->uri);
    } else {
        out->parse_status = STATUS_REQUEST_URI_TOO_LONG;
        goto CATCH_PARSE;
    }

    // バージョン
    TRY_IF(stream_take(buff, "HTTP/1.0", INSENSITIVE)) {
        out->version = HTTP_VER_1_0;
        printf("version: 1.0\n");
    } TRY_ELIF(stream_take(buff, "HTTP/1.1", INSENSITIVE)) {
        out->version = HTTP_VER_1_1;
        printf("version: 1.1\n");
    } else {
        out->version = HTTP_VER_UNKNOWN;
        out->parse_status = STATUS_HTTP_VERSION_NOT_SUPPORTED;
        goto CATCH_PARSE;
    }

    // 改行必須
    TRY_IF(stream_take_newline(buff)) {
    } else {
        out->parse_status = STATUS_BAD_REQUEST;
        goto CATCH_PARSE;
    }

    // ヘッダ
    out->connection = CONNECTION_UNSPECIFIED;

    while (1) {
        TRY_IF(stream_take_headname(buff, name, name_len)) {
            // 終端
            if (strlen(name) == 0) {
                printf("end of header.\n");
                break;
            } else {
                printf("header: %s\n", name);
            }
            // Connectionヘッダ
            if(!strcmp(name, "connection")) {
                TRY_IF(stream_take(buff, "Keep-Alive", INSENSITIVE)) {
                    out->connection = CONNECTION_KEEP_ALIVE;
                    printf("connection: Keep-Alive\n");
                } TRY_ELIF((stream_take(buff, "Close", INSENSITIVE))) {
                    out->connection = CONNECTION_CLOSE;
                    printf("connection: Close\n");
                } else {
                    out->connection = CONNECTION_UNKOWN;
                    printf("connection: Unkown\n");
                }
            }
            // 次のヘッダ行を見つける
            TRY_IF(stream_find_head(buff)) {
            } else {
                out->parse_status = STATUS_BAD_REQUEST;
                goto CATCH_PARSE;
            }
        } else {
            out->parse_status = STATUS_REQUEST_HEADER_FIELD_TOO_LARGE;
            goto CATCH_PARSE;
        }
    }
    return 0;

 CATCH_FATAL:
    out->parse_status = STATUS_BAD_REQUEST;
    return ERR_FATAL;
 CATCH_PARSE:
    return ERR_PARSE;
}

/* pathの拡張子を小文字で取得 */
static void get_ext_lower_case(char *path, char *ext, int ext_len)
{
    size_t len = strlen(path);
    int i, j, k;
    for (i = 1; i <= ext_len; i ++) {
        if(i > len || path[len - i] == '.' || path[len - i] == '/') {
            break;
        }
    }
    for (j = len - i + 1, k = 0; j <= len; j++, k++) {
        ext[k] = tolower(path[j]);
    }
}

/* 拡張子を見てMINEタイプを判定 */
static mine_type path_to_mine_type(char *path)
{
    char ext[10];
    get_ext_lower_case(path, ext, 10);
    if (!strcmp(ext, "htm") || !strcmp(ext, "html")) {
        return MINE_TEXT_HTML;
    }
    if (!strcmp(ext, "xml") || !strcmp(ext, "xhtml") || !strcmp(ext, "xhtm")) {
        return MINE_APPLICATION_XHTML_XML;
    }
    if (!strcmp(ext, "gif")) {
        return MINE_IMAGE_GIF;
    }
    if (!strcmp(ext, "jpg") || !strcmp(ext, "jpeg")) {
        return MINE_IMAGE_JPEG;
    }
    if (!strcmp(ext, "png")) {
        return MINE_IMAGE_PNG;
    }
    if (!strcmp(ext, "mpg") || !strcmp(ext, "mpeg")) {
        return MINE_VIDEO_MPEG;
    }
    if (!strcmp(ext, "pdf")) {
        return MINE_APPLICATION_PDF;
    }
    return MINE_TEXT_PLAIN;
}

/* レスポンスヘッダの生成 */
int make_response_head(http_request_head* req, http_response_head* res, char* root)
{
    res->version = req->version == HTTP_VER_UNKNOWN ?
        HTTP_VER_1_1 : req->version;

   time_t timer;
   struct tm *gmt;
   time(&timer);
   gmt = gmtime(&timer);
   res->date = *gmt;

    if (req->parse_status != STATUS_NULL) {
        res->connection = CONNECTION_CLOSE;
        res->status = req->parse_status;
        return 0;
    }

    // open file
    char path[PATH_LEN];
    char *dst, *src;
    struct stat content_stat;
    size_t root_len, uri_len;
    root_len = strlen(root);
    uri_len = strlen(req->uri);

    if (root_len > PATH_LEN - URI_LEN) {
        fprintf(stderr, "make_response_head: too long root path.\n");
        res->connection = CONNECTION_CLOSE;
        res->status = STATUS_INTERNAL_SERVER_ERROR;
        return 0;
    }

    if (root_len == 0) {
        fprintf(stderr, "make_response_head: empty root path.\n");
        res->connection = CONNECTION_CLOSE;
        res->status = STATUS_INTERNAL_SERVER_ERROR;
        return 0;
    }

    if (root_len + uri_len > PATH_LEN) {
        res->connection = CONNECTION_CLOSE;
        res->status = STATUS_REQUEST_URI_TOO_LONG;
        return 0;
    }

    if (strstr(req->uri, "../") != NULL) {
        res->connection = CONNECTION_CLOSE;
        res->status = STATUS_NOT_FOUND;
        return 0;
    }

    strcpy(path, root);
    src = req->uri[0] == '/' ? req->uri + 1 : req->uri;
    if (path[root_len - 1] == '/') {
        dst = path + root_len;
    } else {
        path[root_len] = '/';
        dst = path + root_len + 1;
    }
    strcpy(dst, src);

    // ?は無視
    while (*dst) {
        if (*dst == '?') {
            *dst = '\0';
            break;
        }
        dst++;
    }

    res->content_fd = open(path, O_RDONLY);
    if (res->content_fd < 0) {
        perror("open");
        res->connection = CONNECTION_CLOSE;
        res->status = STATUS_NOT_FOUND;
        return 0;
    }

    fstat(res->content_fd, &content_stat);
    res->content_length = content_stat.st_size;
    res->content_type = path_to_mine_type(path);
    switch (req->connection)  {
        case CONNECTION_KEEP_ALIVE:
            res->connection = CONNECTION_KEEP_ALIVE;
            break;
        case CONNECTION_CLOSE:
        case CONNECTION_UNKOWN:
        case CONNECTION_UNSPECIFIED:
            res->connection = CONNECTION_CLOSE;
            break;
    }
    res->status = STATUS_OK;
    return 0;
}

const char* version_str(http_version ver)
{
    switch (ver) {
    case HTTP_VER_1_1: return "HTTP/1.1";
    case HTTP_VER_1_0: return "HTTP/1.0";
    default: return "HTTP/1.1";
    }
}

const char* status_str(http_status_code s)
{
    switch (s) {
    case STATUS_OK:
        return "OK";
    case STATUS_BAD_REQUEST:
        return "Bad Request";
    case STATUS_NOT_FOUND:
        return "Not Found";
    case STATUS_REQUEST_URI_TOO_LONG:
        return "Request URI Too Long";
    case STATUS_HTTP_VERSION_NOT_SUPPORTED:
        return "HTTP Version Not Supported";
    case STATUS_NOT_IMPLEMENTED:
        return "Method Not Implemented";
    case STATUS_INTERNAL_SERVER_ERROR:
        return "Internal Server Error";
    case STATUS_REQUEST_HEADER_FIELD_TOO_LARGE:
        return "Request Header Field Too Large";
    default:
        return "Error";
    }
}

const char* connection_str(http_connection conn)
{
    switch (conn) {
    case CONNECTION_KEEP_ALIVE:
        return "Keep-Alive";
    default:
        return "Close";
    }
}

const char* type_str(mine_type type)
{
    switch (type) {
    case MINE_TEXT_PLAIN: return "text/plain";
    case MINE_TEXT_HTML: return "text/html";
    case MINE_APPLICATION_XHTML_XML: return "application/xhtml+xml";
    case MINE_IMAGE_GIF: return "image/gif";
    case MINE_IMAGE_JPEG: return "image/jpeg";
    case MINE_IMAGE_PNG: return "image/png";
    case MINE_VIDEO_MPEG: return "video/mpeg";
    case MINE_APPLICATION_PDF: return "application/pdf";
    default: return "text/plain";
    }
}

/* 送信 */
static int my_send(int socket, const char* str)
{
    ssize_t len = strlen(str);
    ssize_t sent = 0, temp;

    while (sent < len) {
        temp = send(socket, str + sent, len - sent, MSG_NOSIGNAL);
        if (temp > 0) {
            sent += temp;
        } else if (temp < 0 && errno != EINTR) {
            perror("send");
            return ERR_FATAL;
        }
    }
    return 0;
}

/* レスポンスヘッダを送信 */
int send_response_head(int sock, http_response_head *res)
{
    char buff[BUFF_SIZE];

    sprintf(buff, "%s %d %s\r\n",
        version_str(res->version), res->status, status_str(res->status));
    if (my_send(sock, buff) == ERR_FATAL) return ERR_FATAL;

    strftime(buff, sizeof(buff),
        "Date: %a, %d %b %Y %H:%M:%S %Z\n", &res->date);
    if (my_send(sock, buff) == ERR_FATAL) return ERR_FATAL;

    sprintf(buff, "Connection: %s\r\n", connection_str(res->connection));
    if (my_send(sock, buff) == ERR_FATAL) return ERR_FATAL;

    if (res->status == STATUS_OK) {
        sprintf(buff, "Content-Length: %lld\r\n", res->content_length);
        if (my_send(sock, buff) == ERR_FATAL) return ERR_FATAL;

        sprintf(buff, "Content-Type: %s\r\n", type_str(res->content_type));
        if (my_send(sock, buff) == ERR_FATAL) return ERR_FATAL;
    } else if (
        my_send(sock, "Content-Length: 0\r\nContent-Type: texp/plain\r\n")
         == ERR_FATAL
    ) {
         return ERR_FATAL;
    }

    return my_send(sock, "\r\n");
}

/* レスポンスボディを送信 */
int send_response_body(int socket, http_response_head *res)
{
    int w_only;
    char buff[BUFF_SIZE];
    ssize_t r = 0, w = 0, temp_w;
    while(1) {
        w_only = r != w;
        if (!w_only) {
            r = read(res->content_fd, buff, BUFF_SIZE - 1);
            if (r == 0) {
                break;
            } else if (r < 0 && errno != EINTR) {
                perror("read");
                close(res->content_fd);
                return ERR_FATAL;
            }
            if(r < 0) r = 0;
            w = 0;
        }
        temp_w = send(socket, buff + w, r - w, MSG_NOSIGNAL);
        if (temp_w < 0 && errno != EINTR) {
            perror("send");
            close(res->content_fd);
            return ERR_FATAL;
        } else if (temp_w > 0) {
            w += temp_w;
        }
    }
    close(res->content_fd);
    return 0;
}

/* レスポンスオブジェクトの破棄 */
int discard_response(http_response_head *res)
{
    close(res->content_fd);
    return 0;
}
