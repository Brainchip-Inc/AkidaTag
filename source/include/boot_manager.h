#ifndef BOOT_MANAGER_H
#define BOOT_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void confirm_image_if_needed(void);
int init_boot_count(void);
int init_setting_sub_system(void);
void print_image_version(uint8_t area_id, const char *name);

uint32_t boot_count_get_total(void);
uint32_t boot_count_get_firmware(void);
uint32_t boot_count_get_watchdog(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_MANAGER_H */
