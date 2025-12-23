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

/* Flag to write BLE transferred data into SRAM buffer first or write directly
 * into SPI-Flash */
#define WRITE_SRAM_CHUNKS 1

// 244 KB i.e multiples of 244 bytes to ease the BLE operations
#define SRAM_BUFFER_SIZE  249856

/**
 * @brief Initialize the File Transfer Service.
 *
 * @return 0 on success, negative errno on failure.
 */
int file_transfer_init(void);



#endif /* FILE_TRANSFER_H_ */
