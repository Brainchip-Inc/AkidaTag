#ifndef LITTLEFS_STORAGE_H
#define LITTLEFS_STORAGE_H

#include <zephyr/fs/fs.h>
#include <zephyr/shell/shell.h>
/* Export the mount point so main.c knows where files are stored */
extern struct fs_mount_t lfs_storage_mnt;

/* High-level initialization function */
int storage_init(void);

void test_create_file(void);
void read_and_print_file(const char *path);

int cmd_dir(const struct shell *shell, size_t argc, char **argv);
int cmd_mkfs(const struct shell *shell, size_t argc, char **argv);

#endif // LITTLEFS_STORAGE_H