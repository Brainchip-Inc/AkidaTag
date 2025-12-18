#ifndef FILE_TRANSFER_H_
#define FILE_TRANSFER_H_

/*
File Transfer Service allows sending a file from a central device (e.g., phone)
to the nRF BLE peripheral in chunks via a custom GATT characteristic.

The service is a custom BLE service with one write-only characteristic.
Each write appends data to an internal file buffer, which can be accessed by
the application after transfer completes.
*/

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

// UUID definitions
#define BT_UUID_FILE_TRANSFER_SERVICE_VAL \
	BT_UUID_128_ENCODE(0xf000aa00, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_CHAR_VAL \
	BT_UUID_128_ENCODE(0xf000aa01, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_ACK_CHAR_VAL \
	BT_UUID_128_ENCODE(0xf000aa02, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_CTRL_CHAR_VAL \
	BT_UUID_128_ENCODE(0xf000aa03, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_SIZE_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa04, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define APP_CHAR_UUID_VAL \
    BT_UUID_128_ENCODE(0xf000aa05, 0x0451, 0x4000, 0xb000, 0x000000000000)    

#define BT_UUID_FILE_TRANSFER_SERVICE     BT_UUID_DECLARE_128(BT_UUID_FILE_TRANSFER_SERVICE_VAL)
#define BT_UUID_FILE_TRANSFER_CHAR        BT_UUID_DECLARE_128(BT_UUID_FILE_TRANSFER_CHAR_VAL)
#define BT_UUID_FILE_TRANSFER_ACK_CHAR    BT_UUID_DECLARE_128(BT_UUID_FILE_TRANSFER_ACK_CHAR_VAL)
#define BT_UUID_FILE_TRANSFER_CTRL_CHAR   BT_UUID_DECLARE_128(BT_UUID_FILE_TRANSFER_CTRL_CHAR_VAL)
#define BT_UUID_FILE_TRANSFER_SIZE_CHAR   BT_UUID_DECLARE_128(BT_UUID_FILE_TRANSFER_SIZE_CHAR_VAL)
#define APP_CHAR_UUID   BT_UUID_DECLARE_128(APP_CHAR_UUID_VAL)

// API declarations
void send_ack_to_host(uint8_t ack_code);

ssize_t file_transfer_write(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *buf, uint16_t len,
                            uint16_t offset, uint8_t flags);
ssize_t get_app_index(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *app, uint16_t len,
                            uint16_t offset, uint8_t flags);
ssize_t get_file_size(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len,
                               uint16_t offset, uint8_t flags);

/**
 * @brief Initialize the File Transfer Service.
 *
 * @return 0 on success, negative errno on failure.
 */
int file_transfer_init(void);

#endif /* FILE_TRANSFER_H_ */
