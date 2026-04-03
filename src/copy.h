#ifndef COPY_H
#define COPY_H

void copy_files(char *source, char *destination);
int is_empty_dir(char *path);
int directory_exists(char *path);
void read_dir(char *source_dir, char *destination_dir, char *real_source, char *real_dest);
void monitor_folder(char *src, char *dst);

#endif
