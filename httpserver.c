#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#include "http.h"
#include "stream.h"

#ifndef _REENTRANT
#define _REENTRANT
#endif

#ifndef CLIENT_MAX
#define CLIENT_MAX 32
#endif

char* root_dir;

/* 1スレッド1クライアントで対応。 */
int serve(void* param)
{
    int socket = (intptr_t) param;
    // Mac OS Xなど（http.h参照）
    #ifdef NON_MSG_NOSIGNAL
        int set = 1;
        setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
    #endif

    http_request_head req;
    http_response_head res;
    res.connection = CONNECTION_KEEP_ALIVE;
    stream_t buff;
    stream_init(&buff, socket);

    while (res.connection != CONNECTION_CLOSE && !buff.closed) {
        stream_reset(&buff);
        printf("[%d]parse_request_head...\n", socket);
        if (parse_request_head(&buff, &req) == ERR_FATAL) {
            fprintf(stderr, "parse_request_head: fatal error\n");
            break;
        }
        printf("[%d]make_response_head...\n", socket);
        if (make_response_head(&req, &res, root_dir) == ERR_FATAL) {
            fprintf(stderr, "make_response_head: fatal error\n");
            break;
        }
        printf("[%d]send_response_head...\n", socket);
        if (send_response_head(socket, &res) == ERR_FATAL) {
            fprintf(stderr, "send_response_head: fatal error\n");
            break;
        }
        if (res.status == STATUS_OK && req.method == METHOD_GET) {
            printf("[%d]send_response_body...\n", socket);
            if (send_response_body(socket, &res) == ERR_FATAL) {
                fprintf(stderr, "send_response_body: fatal error\n");
                break;
            }
        }
        discard_response(&res);
        printf("[%d]sent: %d\n", socket, res.status);
    }
    close(socket);
    printf("socket %d:close\n", socket);
    return 0;
}

/* argv[1]: port, argv[2]: root path. */
int main(int argc, char* argv[])
{
    // argcをチェック
    if (argc != 3) {
        errno = EINVAL;
        perror("argv");
        return -1;
    }

    // ドキュメントルートを調べる
    struct stat dir_stat;
    if(stat(argv[2], &dir_stat) || !S_ISDIR(dir_stat.st_mode)) {
        fprintf(stderr, "%s: Invalid document root path.", argv[2]);
        return -1;
    }
    root_dir = argv[2];

    // ポート番号
    int port = atoi(argv[1]);
    if (port >= 1 << 16) {
        fprintf(stderr, "%s: Port number is too large.\n", argv[1]);
        return -1;
    }
    printf("Server started on port: %d\n", port);

    // ソケット
    int sock_fd;
    sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    // アドレスとポート
    socklen_t addr_len = sizeof(struct sockaddr_in);
    struct sockaddr_in addr;
    addr.sin_family	= AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_fd, (struct sockaddr *)&addr, addr_len)) {
        perror("bind");
        return -1;
    }

    // 受信＆送信
    int accept_fd;
    pthread_t thread;
    struct sockaddr_in from;
    struct sockaddr *from_ptr = (struct sockaddr *)&from;
    socklen_t from_len;

    // 接続待ち
    if (listen(sock_fd, CLIENT_MAX)) {
        perror("listen");
        return -1;
    }

    // 終了はSIGINTで
    while (1) {
        // 接続
        accept_fd = accept(sock_fd, from_ptr, &from_len);
        if (accept_fd < 0) {
            perror("accept");
            continue;
        } else {
            pthread_create(&thread, NULL,
                           (void*(*)(void *))serve, (void *)(intptr_t)accept_fd);
        }
    }
    return 0;
 }
