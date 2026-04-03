#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "copy.h"
#include "utils.h"

// Funkcja sprawdza czy istnieje kopia folderu src w katalogu docelowym dst
int is_duplicate(state_of_copies* st, char* src, char* dst)
{
    for (int i = 0; i < st->count; i++)
    {
        if (strcmp(st->copies[i].source, src) == 0 && strcmp(st->copies[i].target, dst) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Funkcja sprawdza czy katalog docelowy znajduje się wewnątrz katalogu źródłowego
int is_in_folder(char* src, char* dst)
{
    size_t n = strlen(src);
    if (strncmp(dst, src, n) == 0 && (dst[n] == '/' || dst[n] == '\0'))
    {
        return 1;
    }
    return 0;
}

// Funkcja zapisuje nową aktywną kopię w strukturze state_of_copies
int register_of_copies(state_of_copies* st, char* src, char* dst, pid_t pid)
{
    if (is_in_folder(src, dst))
    {
        return -3;
    }
    if (st->count >= MAX_COPIES)
    {
        return -2;
    }
    // Nie można dodać identycznej kopii drugi raz
    if (is_duplicate(st, src, dst))
    {
        return -1;
    }
    strncpy(st->copies[st->count].source, src, MAX_PATH);
    st->copies[st->count].source[MAX_PATH - 1] = '\0';

    strncpy(st->copies[st->count].target, dst, MAX_PATH);
    st->copies[st->count].target[MAX_PATH - 1] = '\0';

    st->copies[st->count].pid = pid;
    st->count++;
    return 0;
}

// Funkcja wypisuje aktywne kopie
void list(state_of_copies* st)
{
    if (st->count == 0)
    {
        printf("Brak aktywnych kopii.\n\n");
        return;
    }
    printf("Foldery z kopiami:\n");
    for (int i = 0; i < st->count; i++)
    {
        printf("%d, Source: %s, Target: %s\n\n", i, st->copies[i].source, st->copies[i].target);
    }
}

// Funkcja zatrzymuje wskazaną kopię i usuwa ją z listy aktywnych kopii
int end(state_of_copies* st, char* src, char* dst)
{
    for (int i = 0; i < st->count; i++)
    {
        if (strcmp(st->copies[i].source, src) == 0 && strcmp(st->copies[i].target, dst) == 0)
        {
            // Wysyłanie sygnału SIGTERM do procesu, któru obsługuje tę kopię
            if (kill(st->copies[i].pid, SIGTERM) == -1)
            {
                perror("kill");
            }
            st->copies[i] = st->copies[st->count - 1];
            st->count--;
            return 0;
        }
    }
    return -1;
}

// Funkcja robi początkowe kopiowanie i uruchamia monitorowanie katalogu
void add(char* source, char* destination)
{
    char real_src[MAX_PATH];
    char real_dst[MAX_PATH];
    if (realpath(source, real_src) == NULL)
    {
        perror("realpath");
        return;
    }
    if (directory_exists(destination))
    {
        if (!is_empty_dir(destination))
        {
            printf("Katalog nie jest pusty: %s\n", destination);
            return;
        }
    }
    else
    {
        if (mkdir(destination, 0777) == -1)
        {
            perror("mkdir");
            return;
        }
    }

    if (realpath(destination, real_dst) == NULL)
    {
        perror("realpath");
        return;
    }
    if (is_in_folder(real_src, real_dst))
    {
        return;
    }

    read_dir(real_src, real_dst, real_src, real_dst);
    monitor_folder(real_src, real_dst);
}

// Usuwa obiekt, jeśli to katalog usuwa go rekurencyjnie, a jeśli plik lub symlink
// to usuwa go unlink()
int remove_all(char* path)
{
    struct stat filestat;
    if (lstat(path, &filestat) == -1)
    {
        // jeśli path nie istnieje to zwracamy 0 i uznajemy usuwanie za zakończone
        if (errno == ENOENT)
        {
            return 0;
        }
        // Jeśli lstat zwrócił błąd to sygnalizujemy to
        else
        {
            return -1;
        }
    }
    if (S_ISDIR(filestat.st_mode))
    {
        DIR* dirp;
        struct dirent* dp;
        if ((dirp = opendir(path)) == NULL)
        {
            return -1;
        }
        while ((dp = readdir(dirp)))
        {
            if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            {
                continue;
            }
            char buf[MAX_PATH];
            snprintf(buf, sizeof(buf), "%s/%s", path, dp->d_name);
            if (remove_all(buf) == -1)
            {
                closedir(dirp);
                return -1;
            }
        }
        if (closedir(dirp))
        {
            perror("closedir");
            return -1;
        }
        return rmdir(path);
    }
    return unlink(path);
}

// Funkcja kopiuje obiekt src do dst, może to być katalog, plik lub symlink
int copy(char* src, char* dst, char* real_src, char* real_dst)
{
    struct stat filestat;
    if (lstat(src, &filestat) == -1)
    {
        return -1;
    }
    if (S_ISDIR(filestat.st_mode))
    {
        if (mkdir(dst, filestat.st_mode) == -1 && errno != EEXIST)
        {
            return -1;
        }
        DIR* dirp;
        struct dirent* dp;
        if ((dirp = opendir(src)) == NULL)
        {
            return -1;
        }
        while ((dp = readdir(dirp)))
        {
            if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            {
                continue;
            }
            char copy1[MAX_PATH];
            char copy2[MAX_PATH];
            snprintf(copy1, sizeof(copy1), "%s/%s", src, dp->d_name);
            snprintf(copy2, sizeof(copy2), "%s/%s", dst, dp->d_name);
            if (copy(copy1, copy2, real_src, real_dst) == -1)
            {
                closedir(dirp);
                return -1;
            }
        }
        if (closedir(dirp))
        {
            perror("closedir");
            return -1;
        }
    }

    else if (S_ISREG(filestat.st_mode))
    {
        copy_files(src, dst);
    }
    else if (S_ISLNK(filestat.st_mode))
    {
        char link[MAX_PATH];
        ssize_t n = readlink(src, link, sizeof(link) - 1);
        if (n == -1)
        {
            return -1;
        }
        link[n] = '\0';
        ssize_t length = strlen(real_src);
        // Sprawdzenie czy symlink wskazuje do wnętrza źródła
        if (link[0] == '/' && strncmp(link, real_src, strlen(real_src)) == 0 &&
            (link[length] == '/' || link[length] == '\0'))
        {
            char new[MAX_PATH];
            snprintf(new, sizeof(new), "%s%s", real_dst, link + strlen(real_src));
            remove_all(dst);
            if (symlink(new, dst) == -1)
            {
                return -1;
            }
        }
        else
        {
            remove_all(dst);
            if (symlink(link, dst) == -1)
            {
                return -1;
            }
        }
    }
    return 0;
}

// Funkcja przywraca kopię zapasową
int restoree(char* source, char* target)
{
    char reall_src[MAX_PATH];
    char reall_dst[MAX_PATH];
    if (!realpath(source, reall_src))
    {
        perror("realpath");
        return -1;
    }
    if (!realpath(target, reall_dst))
    {
        perror("realpath");
        return -1;
    }

    if (strcmp(reall_src, "/") == 0)
    {
        fprintf(stderr, "Nie można robić restore do /\n");
        return -1;
    }
    if (strcmp(reall_src, reall_dst) == 0)
    {
        fprintf(stderr, "Source i target są takie same\n");
        return -1;
    }
    DIR* dirp;
    if ((dirp = opendir(reall_src)) == NULL)
    {
        perror("opendir");
        return -1;
    }
    struct dirent* dp;
    while ((dp = readdir(dirp)))
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
        {
            continue;
        }
        char path[MAX_PATH];
        int n = snprintf(path, sizeof(path), "%s/%s", reall_src, dp->d_name);
        if (n < 0 || (size_t)n >= sizeof(path))
        {
            fprintf(stderr, "Ścieżka za długa\n");
            continue;
        }
        if (remove_all(path) == -1)
        {
            closedir(dirp);
            perror("remove_all");
            return -1;
        }
    }
    if (closedir(dirp))
    {
        perror("closedir");
        return -1;
    }

    return copy(reall_dst, reall_src, reall_dst, reall_src);
}
