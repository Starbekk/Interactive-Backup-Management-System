#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "commands.h"
#include "utils.h"

#define COPY_BUF_LEN 1024
#define INOTIFY_MASK \
    (IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED)

volatile sig_atomic_t stop_inotify = 0;

// Struktura opisująca jednego watcha
typedef struct
{
    int wd;
    char path[MAX_PATH];
} watch;

// Dynamiczna lista watchy
typedef struct
{
    watch *list;
    size_t size;      // ile watchy jest aktualnie w użyciu
    size_t capacity;  // ile maksymalnie można mieć watchy w tablicy
} watch_list;

// Handler sygnałów SIGINT/SIGTERM
void inotify_term_handler(int sig) { stop_inotify = 1; }

// Funkcja inicjalizuje listę watchy
void init_list(watch_list *lista)
{
    lista->capacity = 64;
    lista->size = 0;
    lista->list = calloc(lista->capacity, sizeof(watch));
    if (!lista->list)
    {
        ERR("calloc");
    }
}

// Funkcja dodaje nowy element do listy. W razie potrzeby zwiększa tablicę przy użyciu realloc
void add_list(watch_list *lista, int wd, char *path)
{
    if (lista->size >= lista->capacity)
    {
        lista->capacity *= 2;
        watch *new_list = realloc(lista->list, lista->capacity * sizeof(watch));
        if (!new_list)
        {
            ERR("realloc");
        }
        lista->list = new_list;
    }
    lista->list[lista->size].wd = wd;
    strncpy(lista->list[lista->size].path, path, MAX_PATH);
    lista->list[lista->size].path[MAX_PATH - 1] = '\0';
    lista->size = lista->size + 1;
}

// Funkcja wyszukuje w liście watchy wpis o podanym wd (Watch Descriptor)
char *list_find(watch_list *lista, int wd)
{
    for (size_t i = 0; i < lista->size; i++)
    {
        if (lista->list[i].wd == wd)
        {
            return lista->list[i].path;
        }
    }
    return NULL;
}

// Funkcja usuwa wszystkie watch'e inotify, zwalnia pamięć listy i resetuje strukturę
void free_list(int fd, watch_list *lista)
{
    if (!lista || !lista->list)
    {
        return;
    }
    for (size_t i = 0; i < lista->size; i++)
    {
        if (inotify_rm_watch(fd, lista->list[i].wd) == -1)
        {
            // ten watch nie istnieje albo fd już zamknięty/niepoprawny
            if (errno != EINVAL && errno != EBADF)
            {
                perror("inotify_rm_watch");
            }
        }
    }
    free(lista->list);
    lista->list = NULL;
    lista->size = 0;
    lista->capacity = 0;
}

// Funkcja rekurencyjnie dodaje watch inotify na katalog i jego podkatalogi
void recursive_watch(int fd, watch_list *lista, char *path)
{
    int wd;
    if ((wd = inotify_add_watch(fd, path, INOTIFY_MASK)) == -1)
    {
        perror("inotify_add_watch");
        return;
    }
    add_list(lista, wd, path);
    DIR *dirp;
    struct dirent *dp; //Wskaźnik do jednego wpisu katalogu
    struct stat filestat; //Struktura na metadane pliku/katalogu/itp.
    if ((dirp = opendir(path)) == NULL)
    {
        perror("opendir");
        return;
    }
    while ((dp = readdir(dirp)) != NULL)
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
        {
            continue;
        }
        char fullpath[MAX_PATH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, dp->d_name);
        if (lstat(fullpath, &filestat))
        {
            perror("Lstat");
            continue;
        }
        if (S_ISDIR(filestat.st_mode))
        {
            recursive_watch(fd, lista, fullpath);
        }
    }
    if (closedir(dirp))
    {
        perror("closedir");
    }
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0) //EOF
            return len; //Zwracamy to co przeczytaliśmy
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

// Funkcja kopiuje plik (z source do destination)
void copy_files(char *source, char *destination)
{
    int fd1 = open(source, O_RDONLY);
    if (fd1 == -1)
    {
        ERR("open");
    }
    int fd2 = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd2 == -1)
    {
        close(fd1);
        ERR("open");
    }
    char buf[COPY_BUF_LEN];
    while (1)
    {
        const ssize_t size = bulk_read(fd1, buf, COPY_BUF_LEN);
        if (size == -1)
        {
            perror("bulk_read");
            break;
        }
        if (size == 0)
        {
            break;
        }
        if (bulk_write(fd2, buf, size) == -1)
        {
            perror("bulk_write");
            break;
        }
    }

    if (close(fd1) == -1)
    {
        perror("close");
    }
    if (close(fd2) == -1)
    {
        perror("close");
    }
}

// Funkcja rekurencyjnie kopiuje zawartość katalogu source dir do katalogu destination_dir
void read_dir(char *source_dir, char *destination_dir, char *real_source, char *real_dest)
{
    DIR *dirp;
    struct dirent *dp;
    struct stat filestat;
    if ((dirp = opendir(source_dir)) == NULL)
    {
        ERR("opendir");
    }
    do
    {
        errno = 0;
        if ((dp = readdir(dirp)) != NULL)
        {
            if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            {
                continue;
            }
            char dst[MAX_PATH];
            char fullpath[MAX_PATH];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", source_dir, dp->d_name);
            snprintf(dst, sizeof(dst), "%s/%s", destination_dir, dp->d_name);
            if (lstat(fullpath, &filestat))
            {
                perror("lstat");
                continue;
            }
            if (S_ISREG(filestat.st_mode))
            {
                copy_files(fullpath, dst);
            }
            else if (S_ISDIR(filestat.st_mode))
            {
                if (mkdir(dst, filestat.st_mode) == -1 && errno != EEXIST)
                {
                    ERR("mkdir");
                }

                read_dir(fullpath, dst, real_source, real_dest);
            }
            else if (S_ISLNK(filestat.st_mode))
            {
                char link[MAX_PATH];
                ssize_t length = readlink(fullpath, link, sizeof(link) - 1); //Ile bajtów ma cel symlinka
                if (length == -1)
                {
                    ERR("readlink");
                }
                link[length] = '\0';
                char *new_link = link;
                char new_target[MAX_PATH];
                // Sprawdzenie czy jest to symlink absolutny
                if (link[0] == '/')
                {
                    size_t src_length = strlen(real_source);
                    // Sprawdzenie czy symlink wskazuje do wnętrza źródła
                    if (strncmp(link, real_source, src_length) == 0 &&
                        (link[src_length] == '/' || link[src_length] == '\0'))
                    {
                        //Zapisujemy nowy symlink (wskaźnik na znak nr src_Lentgh w stringu link to 3 argument)
                        snprintf(new_target, sizeof(new_target), "%s%s", real_dest, link + src_length);
                        new_link = new_target;
                    }
                }
                // Usuwamy istniejący wpis, aby można było utworzyć nowe dowiązanie symboliczne o nazwie dst
                remove_all(dst);
                if (symlink(new_link, dst)) //Tworzymy plik new_link, który wskazuje na dst
                {
                    ERR("symlink");
                }
            }
        }
    } while (dp != NULL);
    if (errno != 0)
    {
        ERR("readdir");
    }
    if (closedir(dirp))
    {
        ERR("closedir");
    }
}

// Funkcja monitoruje katalog źródłowy przy użyciu inotify i na bieżąco synchronizuje
// zmiany do katalogu docelowego
void monitor_folder(char *src, char *dst)
{
    stop_inotify = 0;

    sethandler(inotify_term_handler, SIGTERM);
    sethandler(inotify_term_handler, SIGINT);

    // Tworzenie deskryptora inotify
    int fd = inotify_init();
    if (fd == -1)
    {
        ERR("inotify_init");
    }
    watch_list lista;
    init_list(&lista);
    recursive_watch(fd, &lista, src);
    // bufor na eventy
    char buf[MAX_PATH];
    int root_wd = lista.list[0].wd;
    while (!stop_inotify)
    {
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len < 0)
        {
            if (errno == EINTR) // Wywołanie systemowe zostało przerwane przez sygnał
            {
                continue;
            }
            ERR("read");
        }
        char *p = buf;
        while (p < buf + len)
        {
            struct inotify_event *event = (struct inotify_event *)p;
            // Warunek kończenia obserwowania katalogu
            if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF && event->wd == root_wd))
            {
                stop_inotify = 1;
                break;
            }
            if (event->mask & IN_IGNORED && event->wd == root_wd)
            {
                stop_inotify = 1;
                break;
            }
            char *find = list_find(&lista, event->wd);
            if (find && event->len > 0)
            {
                char src_path[MAX_PATH];
                char dst_path[MAX_PATH];
                snprintf(src_path, sizeof(src_path), "%s/%s", find, event->name);
                snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, src_path + strlen(src) + 1);

                if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                {
                    remove_all(dst_path);
                }
                if (event->mask & (IN_CREATE | IN_MODIFY))
                {
                    struct stat filestat;
                    if (lstat(src_path, &filestat) == 0)
                    {
                        remove_all(dst_path);
                        if (S_ISREG(filestat.st_mode))
                        {
                            copy_files(src_path, dst_path);
                        }
                        else if (S_ISDIR(filestat.st_mode))
                        {
                            if (mkdir(dst_path, filestat.st_mode) == -1)
                            {
                                if (errno != EEXIST)
                                {
                                    perror("mkdir");
                                    continue;
                                }
                            }
                            recursive_watch(fd, &lista, src_path);
                            read_dir(src_path, dst_path, src, dst);
                        }
                        else if (S_ISLNK(filestat.st_mode))
                        {
                            char link[MAX_PATH];
                            ssize_t link_read = readlink(src_path, link, sizeof(link) - 1);
                            if (link_read >= 0)
                            {
                                link[link_read] = '\0';
                                // symlink do wewnątrz src
                                if (link[0] == '/' && strncmp(link, src, strlen(src)) == 0 &&
                                    (link[strlen(src)] == '/' || link[strlen(src)] == '\0'))
                                {
                                    char new_target[MAX_PATH];
                                    snprintf(new_target, sizeof(new_target), "%s%s", dst, link + strlen(src));
                                    if (symlink(new_target, dst_path) == -1)
                                    {
                                        perror("symlink");
                                    }
                                }
                                // inne symlinki
                                else
                                {
                                    if (symlink(link, dst_path) == -1)
                                    {
                                        perror("symlink");
                                    }
                                }
                            }
                        }
                    }
                }
                if (event->mask & IN_MOVED_TO)
                {
                    struct stat filestat;
                    if (lstat(src_path, &filestat) == 0)
                    {
                        remove_all(dst_path);
                        if (S_ISREG(filestat.st_mode))
                        {
                            copy_files(src_path, dst_path);
                        }
                        else if (S_ISDIR(filestat.st_mode))
                        {
                            if (mkdir(dst_path, filestat.st_mode) == -1)
                            {
                                if (errno != EEXIST)
                                {
                                    perror("mkdir");
                                }
                            }
                            recursive_watch(fd, &lista, src_path);
                            read_dir(src_path, dst_path, src, dst);
                        }
                        else if (S_ISLNK(filestat.st_mode))
                        {
                            char link[MAX_PATH];
                            ssize_t link_read = readlink(src_path, link, sizeof(link) - 1);
                            if (link_read >= 0)
                            {
                                link[link_read] = '\0';

                                if (link[0] == '/' && strncmp(link, src, strlen(src)) == 0 &&
                                    (link[strlen(src)] == '/' || link[strlen(src)] == '\0'))
                                {
                                    char new_target[MAX_PATH];
                                    snprintf(new_target, sizeof(new_target), "%s%s", dst, link + strlen(src));
                                    if (symlink(new_target, dst_path) == -1)
                                    {
                                        perror("symlink");
                                    }
                                }
                                else
                                {
                                    if (symlink(link, dst_path) == -1)
                                    {
                                        perror("symlink");
                                    }
                                }
                            }
                        }
                    }
                }
                //Sprawdzenie czy katalog został przeniesiony
                if ((event->mask & (IN_MOVED_FROM | IN_MOVED_TO)) && (event->mask & IN_ISDIR))
                {
                    free_list(fd, &lista);
                    init_list(&lista);
                    recursive_watch(fd, &lista, src);
                }
            }
            p += sizeof(struct inotify_event) + event->len;
        }
    }
    free_list(fd, &lista);
    close(fd);
}

// Funkcja sprawdza czy katalog jest pusty
int is_empty_dir(char *path)
{
    DIR *dirp;
    struct dirent *dp;

    if ((dirp = opendir(path)) == NULL)
    {
        ERR("opendir");
    }
    while ((dp = readdir(dirp)) != NULL)
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
        {
            continue;
        }
        closedir(dirp);
        return 0;
    }
    if (closedir(dirp))
    {
        ERR("closedir");
    }
    return 1;
}

// Funkcja sprawdza czy katalog istnieje
int directory_exists(char *path)
{
    struct stat st;
    if (stat(path, &st) == -1)
    {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}
