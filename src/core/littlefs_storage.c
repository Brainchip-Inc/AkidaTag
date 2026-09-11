#include "littlefs_storage.h"
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(littlefs_storage, CONFIG_LOG_DEFAULT_LEVEL);

/* Define internal LittleFS data structures */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage_data);

/* Define the mount point structure */
struct fs_mount_t lfs_storage_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage_data,
    .storage_dev = (void *)FIXED_PARTITION_ID(littlefs_storage),
    .mnt_point = "/ext",
};

/* Code to Mount the filesytem */
int storage_init(void) {
  int rc = fs_mount(&lfs_storage_mnt);

  if (rc == -ENODEV || rc == -EIO || rc == -EINVAL) {
    LOG_WRN("Mount failed (%d). Formatting flash...", rc);
    rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)lfs_storage_mnt.storage_dev,
                 &storage_data, 0);
    if (rc == 0) {
      rc = fs_mount(&lfs_storage_mnt);
    }
  }

  if (rc == 0) {
    LOG_INF("External flash mounted at %s", lfs_storage_mnt.mnt_point);
  } else {
    LOG_ERR("FileSystem init failed: %d", rc);
  }
  return rc;
}

/* code to list all the files present in the file system */
static int lsdir(const char *path) {
  int res;
  struct fs_dir_t dirp;
  static struct fs_dirent entry;

  fs_dir_t_init(&dirp);

  /* Verify fs_opendir() */
  res = fs_opendir(&dirp, path);
  if (res) {
    LOG_ERR("Error opening dir %s [%d]\n", path, res);
    return res;
  }

  LOG_INF("\nListing dir %s ...\n", path);
  for (;;) {
    /* Verify fs_readdir() */
    res = fs_readdir(&dirp, &entry);

    /* entry.name[0] == 0 means end-of-dir */
    if (res || entry.name[0] == 0) {
      if (res < 0) {
        LOG_ERR("Error reading dir [%d]\n", res);
      }
      break;
    }

    if (entry.type == FS_DIR_ENTRY_DIR) {
      LOG_INF("[DIR ] %s\n", entry.name);
    } else {
      LOG_INF("[FILE] %s (size = %zu)\n", entry.name, entry.size);
    }
  }

  /* Verify fs_closedir() */
  fs_closedir(&dirp);

  return res;
}

/* function to format the filesystem storage */
static int storage_format(void) {
  int rc;

  /* 1. You MUST unmount before formatting if it was previously mounted */
  fs_unmount(&lfs_storage_mnt);

  /* 2. Perform the format
   * Parameters:
   * - FS_LITTLEFS: The type of filesystem
   * - storage_dev: The partition ID (from your mount struct)
   * - fs_data: The configuration structure (from your mount struct)
   * - flags: Usually 0 for LittleFS
   */
  rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)lfs_storage_mnt.storage_dev,
               lfs_storage_mnt.fs_data, 0);

  if (rc != 0) {
    LOG_ERR("Failed to format LittleFS: %d", rc);
    return rc;
  }

  LOG_INF("LittleFS partition formatted successfully.");

  /* 3. Re-mount after formatting */
  return fs_mount(&lfs_storage_mnt);
}

/* Test code to create the hello.txt file in file system */
void test_create_file(void) {
  struct fs_file_t file;
  fs_file_t_init(&file);

  int rc = fs_open(&file, "/ext/hello.txt", FS_O_CREATE | FS_O_WRITE);
  if (rc == 0) {
    char *text = "nRF5340 LittleFS Test";
    fs_write(&file, text, strlen(text));
    fs_close(&file);
    LOG_INF("Test file created successfully!\n");
  } else {
    LOG_ERR("Failed to create test file: %d\n", rc);
  }
}

/* Code to read the passed file and print the content */
void read_and_print_file(const char *path) {
  struct fs_file_t file;
  struct fs_dirent info;
  int rc;

  fs_file_t_init(&file);

  /* 1. Get file size first to allocate a buffer */
  rc = fs_stat(path, &info);
  if (rc != 0) {
    LOG_ERR("Error: Could not find file %s (%d)\n", path, rc);
    return;
  }

  /* 2. Open for reading */
  rc = fs_open(&file, path, FS_O_READ);
  if (rc != 0) {
    LOG_ERR("Error: Failed to open %s for reading (%d)\n", path, rc);
    return;
  }

  /* 3. Read content into a temporary buffer */
  /* Note: For very large files, read in small chunks (e.g., 64 bytes) instead
   */
  char buffer[info.size + 1];
  rc = fs_read(&file, buffer, info.size);

  if (rc >= 0) {
    buffer[rc] = '\0'; // Null-terminate the string
    LOG_INF("--- Content of %s ---\n", path);
    LOG_INF("%s\n", buffer);
    LOG_INF("----------------------\n");
  } else {
    LOG_ERR("Error reading file: %d\n", rc);
  }

  fs_close(&file);
}

int cmd_dir(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    LOG_ERR("invalid command ");
    return -EINVAL;
  }
  lsdir(lfs_storage_mnt.mnt_point);
  return 0;
}

int cmd_mkfs(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    LOG_ERR("invalid command ");
    return -EINVAL;
  }
  storage_format();
  return 0;
}

int cmd_create_test_file(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    LOG_ERR("invalid command ");
    return -EINVAL;
  }
  test_create_file();
  return 0;
}

int cmd_print_file(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 2) {
    LOG_ERR("invalid command ");
    return -EINVAL;
  }
  char *file_path = argv[1];
  read_and_print_file(file_path);
  return 0;
}

SHELL_CMD_REGISTER(dir, NULL, "File System listing", cmd_dir);
SHELL_CMD_REGISTER(mkfs, NULL, "Make File System", cmd_mkfs);
SHELL_CMD_REGISTER(test_file, NULL, "test file creation", cmd_create_test_file);
SHELL_CMD_REGISTER(print_file, NULL, "print file", cmd_print_file);