#ifndef COMMANDS_H
#define COMMANDS_H

#include <sys/types.h>
#include "utils.h"

void add(char *source, char *destination);
int register_of_copies(state_of_copies *st, char *src, char *dst, pid_t pid);
void list(state_of_copies *st);
int is_duplicate(state_of_copies *st, char *src, char *dst);
int end(state_of_copies *st, char *src, char *dst);
int restoree(char *source, char *backup);
int remove_all(char *path);

#endif
