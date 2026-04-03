#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

// usage() wypisuje listę dostępnych komend
void usage()
{
    fprintf(stdout,
            "Dostępne polecenia:\n"
            "add <source> <target1> <target2>...\n"
            "end <source> <target>\n"
            "list\n"
            "restore <source> <target>\n"
            "exit\n\n");
}
// Ustawianie handlera sygnału
void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}
// Funkcja rozwija "~", gdy nie ma potrzeby rozwijania "~" funkcja zwraca oryginalną ścieżkę
char *home(char *path, char *buf, size_t size)
{
    if (path[0] == '~')
    {
        char *home_ = getenv("HOME");
        if (!home_)
        {
            home_ = "";
        }
        snprintf(buf, size, "%s%s", home_, path + 1);
        return buf;
    }
    return path;
}
// Funkcja dzieli linię polecenia na argumenty i zwraca liczbę argumentów
int split_arguments(char *line, char *argv[], int max)
{
    for (int i = 0; i < max; i++)
    {
        argv[i] = NULL;
    }
    int argc = 0;
    char *p = line;
    while (*p && argc < max)
    {
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        // obsługa "" - podwójnych cudzysłowów
        if (*p == '"')
        {
            p++;
            argv[argc++] = p;

            while (*p && *p != '"')
            {
                p++;
            }
            if (*p == '"')
            {
                *p = '\0';
                p++;
            }
            continue;
        }
        // obsługa '' - pojedynczych cudzysłowów
        else if (*p == '\'')
        {
            p++;
            argv[argc++] = p;
            while (*p && *p != '\'')
            {
                p++;
            }
            if (*p == '\'')
            {
                *p = '\0';
                p++;
            }
            continue;
        }
        // obsługa agrumentu bez cudzysłowów
        else
        {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t')
            {
                p++;
            }
            if (*p)
            {
                *p = '\0';
                p++;
            }
        }
    }
    return argc;
}
