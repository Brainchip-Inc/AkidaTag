#include "littlefs_storage.h"
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>


LOG_MODULE_REGISTER(littlefs_storage, CONFIG_LOG_DEFAULT_LEVEL);

/* 1. Define internal LittleFS data structures */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage_data);

/* 2. Define the mount point structure */
struct fs_mount_t lfs_storage_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage_data,
    .storage_dev = (void *)FIXED_PARTITION_ID(littlefs_storage),
    .mnt_point = "/ext",
};

/* 3. The robust mount function we discussed earlier */
int storage_init(void) {
    int rc = fs_mount(&lfs_storage_mnt);

    if (rc == -ENODEV || rc == -EIO || rc == -EINVAL) {
        LOG_WRN("Mount failed (%d). Formatting flash...", rc);
        rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)lfs_storage_mnt.storage_dev, &storage_data, 0);
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



static int lsdir(const char *path)
{
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

	LOG_PRINTK("\nListing dir %s ...\n", path);
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
			LOG_PRINTK("[DIR ] %s\n", entry.name);
		} else {
			LOG_PRINTK("[FILE] %s (size = %zu)\n",
				   entry.name, entry.size);
		}
	}

	/* Verify fs_closedir() */
	fs_closedir(&dirp);

	return res;
}

static int littlefs_increase_infile_value(char *fname)
{
	uint8_t boot_count = 0;
	struct fs_file_t file;
	int rc, ret;

	fs_file_t_init(&file);
	rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
	if (rc < 0) {
		LOG_ERR("FAIL: open %s: %d", fname, rc);
		return rc;
	}

	rc = fs_read(&file, &boot_count, sizeof(boot_count));
	if (rc < 0) {
		LOG_ERR("FAIL: read %s: [rd:%d]", fname, rc);
		goto out;
	}
	LOG_PRINTK("%s read count:%u (bytes: %d)\n", fname, boot_count, rc);

	rc = fs_seek(&file, 0, FS_SEEK_SET);
	if (rc < 0) {
		LOG_ERR("FAIL: seek %s: %d", fname, rc);
		goto out;
	}

	boot_count += 1;
	rc = fs_write(&file, &boot_count, sizeof(boot_count));
	if (rc < 0) {
		LOG_ERR("FAIL: write %s: %d", fname, rc);
		goto out;
	}

	LOG_PRINTK("%s write new boot count %u: [wr:%d]\n", fname,
		   boot_count, rc);

 out:
	ret = fs_close(&file);
	if (ret < 0) {
		LOG_ERR("FAIL: close %s: %d", fname, ret);
		return ret;
	}

	return (rc < 0 ? rc : 0);
}


static int storage_format(void)
{
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
    rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)lfs_storage_mnt.storage_dev, lfs_storage_mnt.fs_data, 0);
    
    if (rc != 0) {
        LOG_ERR("Failed to format LittleFS: %d", rc);
        return rc;
    }

    LOG_INF("LittleFS partition formatted successfully.");
    
    /* 3. Re-mount after formatting */
    return fs_mount(&lfs_storage_mnt);
}

void test_create_file(void) {
    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, "/ext/hello.txt", FS_O_CREATE | FS_O_WRITE);
    if (rc == 0) {
        char *text = "nRF5340 LittleFS Test";
        fs_write(&file, text, strlen(text));
        fs_close(&file);
        printk("Test file created successfully!\n");
    } else {
        printk("Failed to create test file: %d\n", rc);
    }
}



void read_and_print_file(const char *path) {
    struct fs_file_t file;
    struct fs_dirent info;
    int rc;

    fs_file_t_init(&file);

    /* 1. Get file size first to allocate a buffer */
    rc = fs_stat(path, &info);
    if (rc != 0) {
        printk("Error: Could not find file %s (%d)\n", path, rc);
        return;
    }

    /* 2. Open for reading */
    rc = fs_open(&file, path, FS_O_READ);
    if (rc != 0) {
        printk("Error: Failed to open %s for reading (%d)\n", path, rc);
        return;
    }

    /* 3. Read content into a temporary buffer */
    /* Note: For very large files, read in small chunks (e.g., 64 bytes) instead */
    char buffer[info.size + 1]; 
    rc = fs_read(&file, buffer, info.size);
    
    if (rc >= 0) {
        buffer[rc] = '\0'; // Null-terminate the string
        printk("--- Content of %s ---\n", path);
        printk("%s\n", buffer);
        printk("----------------------\n");
    } else {
        printk("Error reading file: %d\n", rc);
    }

    fs_close(&file);
}


int mkfs_littlefs(void)
{
	return storage_format();
}

int list_littlefs(void)
{
	return lsdir(lfs_storage_mnt.mnt_point);	
}


int cmd_dir(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    printk("invalid command ");
    return -EINVAL;
  }
  list_littlefs();
  return 0;
}

int cmd_mkfs(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    printk("invalid command ");
    return -EINVAL;
  }
  mkfs_littlefs();
  return 0;
}

SHELL_CMD_REGISTER(dir, NULL, "File System listing", cmd_dir);
SHELL_CMD_REGISTER(mkfs, NULL, "Make File System", cmd_mkfs);