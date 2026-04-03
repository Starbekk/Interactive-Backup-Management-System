#ifndef UTILS_H
#define UTILS_H

#define MAX_LEN_LINE 256
#define MAX_PATH 4096
#define MAX_COPIES 128

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void sethandler(void (*f)(int), int sigNo);

typedef struct
{
    char source[MAX_PATH];
    char target[MAX_PATH];
    pid_t pid;
} active_copies;

typedef struct
{
    active_copies copies[MAX_COPIES];
    int count;
} state_of_copies;

void usage(void);
char *home(char *path, char *buf, size_t size);
int split_arguments(char *line, char *argv[], int max);

#endif
