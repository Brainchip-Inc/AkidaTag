#ifndef BOOT_MANAGER_H
#define BOOT_MANAGER_H
#include <stdint.h>
void confirm_image_if_needed(void);
int init_boot_count(void);
int init_setting_sub_system(void);
void print_image_version(uint8_t area_id, const char *name);

#endif // BOOT_MANAGER_H