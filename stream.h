#ifndef STREAM_H
#define STREAM_H

// 大文字小文字を無視するフラグ
#define INSENSITIVE 0
#define SENSITIVE 1

#define BUFF_SIZE 512

typedef struct _stream_t
{
    int socket;
    int head;
    int tail;
    int closed;
    int terminated;
    int next_tail;
    char buff[BUFF_SIZE];
} stream_t;

void stream_init(stream_t* buff, int socket);
void stream_reset(stream_t* buff);
int stream_take(stream_t* buff, const char* match, int sensitive);
int stream_take_token(stream_t* buff, char* dst, int len);
int stream_take_headname(stream_t* buff, char* dst, int len);
int stream_take_newline(stream_t* buff);
int stream_find_head(stream_t* buff);

#endif
