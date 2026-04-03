#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "commands.h"
#include "copy.h"
#include "utils.h"

volatile sig_atomic_t end_program = 0;

void sig_term_handler(int sig) { end_program = 1; }

int main(int argc, char *argv[])
{
    char linia[MAX_LEN_LINE];
    state_of_copies state;
    state.count = 0;
    // Obsługa sygnałów
    sethandler(sig_term_handler, SIGTERM);
    sethandler(sig_term_handler, SIGINT);
    // Ignorowanie sygnałów
    sethandler(SIG_IGN, SIGPIPE);
    sethandler(SIG_IGN, SIGUSR1);
    sethandler(SIG_IGN, SIGUSR2);

    usage();  // Wypisywanie dostępnych poleceń

    while (1)
    {
        if (end_program)
        {
            for (int i = 0; i < state.count; i++)
            {
                if (kill(state.copies[i].pid, SIGTERM) == -1)
                {
                    perror("kill");
                }
            }
            break;
        }
        if (!fgets(linia, sizeof(linia), stdin))
        {
            for (int i = 0; i < state.count; i++)
            {
                if (kill(state.copies[i].pid, SIGTERM) == -1)
                {
                    perror("kill");
                }
            }
            break;
        }
        if (strlen(linia) > 0 && linia[strlen(linia) - 1] == '\n')
        {
            linia[strlen(linia) - 1] = '\0';
        }
        char *arguments[MAX_LEN_LINE + 1];
        // Rozdzielanie argumentów linii
        int arguments_count = split_arguments(linia, arguments, MAX_LEN_LINE);
        arguments[arguments_count] = NULL;
        if (arguments_count == 0)
        {
            continue;
        }
        // Komenda add
        if (strcmp(arguments[0], "add") == 0)
        {
            if (arguments_count < 3)
            {
                usage();
                continue;
            }
            char source_buf[MAX_PATH];
            char destination_buf[MAX_PATH];

            char *source = home(arguments[1], source_buf, sizeof(source_buf));

            char real_src[MAX_PATH];

            if (realpath(source, real_src) == NULL)
            {
                perror("realpath");
                continue;
            }

            char *dst;
            for (int i = 2; i < arguments_count; i++)
            {
                dst = home(arguments[i], destination_buf, sizeof(destination_buf));
                char real_dst[MAX_PATH];

                if (!realpath(dst, real_dst))
                {
                    strncpy(real_dst, dst, MAX_PATH);
                    real_dst[MAX_PATH - 1] = '\0';
                }
                if (directory_exists(real_dst) && !is_empty_dir(real_dst))
                {
                    fprintf(stderr, "Katalog nie jest pusty\n");
                    continue;
                }
                pid_t pid;
                if ((pid = fork()) < 0)
                {
                    perror("fork");
                    continue;
                }
                else if (pid == 0)
                {
                    add(real_src, real_dst);
                    exit(EXIT_SUCCESS);
                }
                else
                {
                    int copy_register = register_of_copies(&state, real_src, real_dst, pid);
                    if (copy_register == -1)
                    {
                        printf("Istnieje już taka kopia\n");
                        kill(pid, SIGTERM);
                    }
                    else if (copy_register == -2)
                    {
                        printf("Przekroczono limit kopii\n");
                        kill(pid, SIGTERM);
                    }
                    else if (copy_register == -3)
                    {
                        printf("Nie można tworzyć kopii wewnątrz katalogu źródłowego\n");
                        kill(pid, SIGTERM);
                    }
                }
            }
        }
        // Komenda end
        else if (strcmp(arguments[0], "end") == 0)
        {
            if (arguments_count < 3)
            {
                usage();
                continue;
            }
            char src_buf[MAX_PATH];
            char dst_buf[MAX_PATH];
            char *src = home(arguments[1], src_buf, sizeof(src_buf));
            char real_src[MAX_PATH];
            if (realpath(src, real_src) == NULL)
            {
                perror("realpath");
                continue;
            }
            for (int i = 2; i < arguments_count; i++)
            {
                char *dst = home(arguments[i], dst_buf, sizeof(dst_buf));
                char real_dst[MAX_PATH];
                if (realpath(dst, real_dst) == NULL)
                {
                    strncpy(real_dst, dst, MAX_PATH);
                    real_dst[MAX_PATH - 1] = '\0';
                }
                int end_ = end(&state, real_src, real_dst);
                if (end_ == -1)
                {
                    printf("Nie ma takiej kopii.\n");
                }
            }
        }

        // Komenda list
        else if (strcmp(arguments[0], "list") == 0)
        {
            list(&state);
        }

        // Komenda restore
        else if (strcmp(arguments[0], "restore") == 0)
        {
            if (arguments_count != 3)
            {
                usage();
                continue;
            }
            char source_buf[MAX_PATH];
            char destination_buf[MAX_PATH];
            char *src = home(arguments[1], source_buf, sizeof(source_buf));
            char *dst = home(arguments[2], destination_buf, sizeof(destination_buf));

            if (restoree(src, dst) == -1)
            {
                fprintf(stderr, "Błąd w restore\n");
            }
        }

        // Komenda exit
        else if (strcmp(arguments[0], "exit") == 0)
        {
            for (int i = 0; i < state.count; i++)
            {
                kill(state.copies[i].pid, SIGTERM);
            }
            break;
        }

        else
        {
            fprintf(stderr, "Niepoprawna komenda: %s\n\n", arguments[0]);
            usage();
        }
    }

    while (wait(NULL) > 0)
    {
    }

    return EXIT_SUCCESS;
}
