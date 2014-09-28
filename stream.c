#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "stream.h"

#define ERR_FATAL -1
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

/* 初期化 */
void stream_init(stream_t* buff, int socket){
    buff->socket = socket;
    buff->head = 0;
    buff->tail = 0;
    buff->next_tail = 0;
    buff->closed = 0;
    buff->terminated = 0;
}
/* リセット */
void stream_reset(stream_t* buff) {
    buff->tail = buff->next_tail;
    buff->terminated = 0;
}

/* CRLF CRLFを見つける。ナイーブに探してOK。 */
static int find_next_tail(stream_t* buff)
{
    char ptrn[] = "\r\n\r\n";
    int idx = -1;
    int i, j = buff->tail;
    if (buff->head < buff->tail) {
        j = 0;
        for (i = buff->tail; i < BUFF_SIZE; i++) {
            idx = ptrn[idx] == buff->buff[i] ? idx + 1 : 0;
            if (ptrn[idx] == '\0') {
                return (i + 1) % BUFF_SIZE;
            }
        }
    }
    for (; j < buff->head; j++) {
        idx = ptrn[idx] == buff->buff[j] ? idx + 1 : 0;
        if (ptrn[idx] == '\0') {
            return (j + 1) % BUFF_SIZE;
        }
    }
    return -1;
}

/* read出来る場合にreadしてバッファを埋める */
static ssize_t stream_update(stream_t* buff)
{
    char temp[BUFF_SIZE];
    ssize_t readed, need, right, left;
    if (buff->closed || buff->terminated
        || (buff->head + 1) % BUFF_SIZE == buff->tail) {
        return 0;
    }
    if (buff->tail <= buff->head) {
        need = BUFF_SIZE - (buff->head - buff->tail) - 1;
    } else {
        need = buff->tail - buff->head - 1;
    }
SYS_READ:
    readed = read(buff->socket, temp, need);
    printf("read: %d\n", (int)readed);
    if (readed == 0) {
        buff->closed = 1;
    } else if (readed > 0) {
        right = MIN(BUFF_SIZE - buff->head, readed);
        memcpy(buff->buff + buff->head, temp, right);
        left = readed - right;
        memcpy(buff->buff, temp + left, left);
        buff->head = (buff->head + readed) % BUFF_SIZE;
        buff->next_tail = find_next_tail(buff);
        if (buff->next_tail > 0) {
            printf("request terminated.\n");
            buff->terminated = 1;
        }
    } else if (errno == EINTR) {
        goto SYS_READ;
    } else {
        perror("read");
        return ERR_FATAL;
    }
    return readed;
}

/* 連続する空白を読み飛ばす。 */
static int stream_skip(stream_t* buff)
{
    while (buff->tail != buff->head &&
            (buff->buff[buff->tail] == ' '
                || buff->buff[buff->tail] == '\t')) {
        buff->tail += 1;
        if (buff->tail == BUFF_SIZE) {
            buff->tail = 0;
        }
    }
    ssize_t update;
    if (buff->tail == buff->head) {
        update = stream_update(buff);
        if (update > 0) {
            return stream_skip(buff);
        } else if (update == ERR_FATAL) {
            return ERR_FATAL;
        }
    }
    // 複数行ヘッダラインに対応
    int size;
    if (buff->tail <= buff->head)
        size = buff->head - buff->tail;
    else
        size = BUFF_SIZE - (buff->tail - buff->head);
    if(size >= 3 
        && buff->buff[buff->tail] == '\r'
        && buff->buff[(buff->tail + 1) % BUFF_SIZE] == '\n'
        && (buff->buff[(buff->tail + 1) % BUFF_SIZE] == ' '
            || buff->buff[(buff->tail + 1) % BUFF_SIZE] == '\t')) {
        buff->tail = buff->tail + 3;
        return stream_skip(buff);
    }
    return 0;
}

static int sensitive_eq(char a, char b, int sensitive)
{
    if (sensitive) {
        return a == b;
    } else {
        return tolower(a) == tolower(b);
    }
}

/* ストリームの先頭がmatchだったら読み飛ばす */
int stream_take(stream_t* buff, const char* match, int sensitive)
{
    if (stream_skip(buff) == ERR_FATAL
        || stream_update(buff) == ERR_FATAL) {
        return ERR_FATAL;
    }
    int tail = buff->tail;
    while (buff->tail != buff->head
            && sensitive_eq(buff->buff[buff->tail], *match, sensitive)
            && *match != '\0') {
        buff->tail += 1;
        match += 1;
        if (buff->tail == BUFF_SIZE) {
            buff->tail = 0;
        }
    }
    if (*match == '\0') {
        return 1;
    } else {
        buff->tail = tail;
        return 0;
    }
}

/* ストリームの先頭の（現在の行内の）トークンを取得 */
int stream_take_token(stream_t* buff, char* dst, int len)
{
    if (stream_skip(buff) == ERR_FATAL
        || stream_update(buff) == ERR_FATAL) {
        return ERR_FATAL;
    }
    int i;
    for (i = 0; i < len; i++) {
        switch (buff->buff[buff->tail]) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            dst[i] = '\0';
            return 1;
        default:
            dst[i] = buff->buff[buff->tail];
            break;
        }
        buff->tail += 1;
        if (buff->tail == BUFF_SIZE) {
            buff->tail = 0;
        }
    }
    // reset
    buff->tail -= i;
    fprintf(stderr, "stream_take_token: Token too long.\n");
    return 0;
}

/* ヘッダのフィールド名を取得。 */
int stream_take_headname(stream_t* buff, char* dst, int len)
{
    if (stream_skip(buff) == ERR_FATAL
            || stream_update(buff) == ERR_FATAL) {
        return ERR_FATAL;
    }
    int i;
    for (i = 0; i < len; i++) {
        switch (buff->buff[buff->tail]) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case ':':
            dst[i] = '\0';
            if (stream_skip(buff) == ERR_FATAL
                    || stream_take(buff, ":", 1) == ERR_FATAL) {
                return ERR_FATAL;
            } else {
                return 1;
            }
        default:
            dst[i] = tolower(buff->buff[buff->tail]);
            break;
        }
        buff->tail += 1;
        if (buff->tail == BUFF_SIZE) {
            buff->tail = 0;
        }
    }
    // reset and throw error
    buff->tail -= i;
    fprintf(stderr, "stream_take_headname: too large header name.\n");
    return 0;
}

/* CRLFを読む。 */
int stream_take_newline(stream_t* buff)
{
    return stream_take(buff, "\r\n", 1);
}

/* 次のヘッダまで移動。 */
int stream_find_head(stream_t* buff)
{
    while (buff->tail != buff->head &&
            buff->buff[buff->tail] != '\r') {
        buff->tail += 1;
        if (buff->tail == BUFF_SIZE) {
            buff->tail = 0;
        }
    }
    if (stream_take_newline(buff) == ERR_FATAL) {
        return ERR_FATAL;
    }
    if (buff->tail == buff->head) {
        return 0;
    } else if (buff->buff[buff->tail] == ' '
            || buff->buff[buff->tail] == '\t') {
        return stream_find_head(buff);
    } else {
        return 1;
    }
}
