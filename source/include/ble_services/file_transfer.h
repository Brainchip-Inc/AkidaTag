#ifndef FILE_TRANSFER_H_
#define FILE_TRANSFER_H_

/*
File Transfer Service allows sending a file from a central device (e.g., phone)
to the nRF BLE peripheral in chunks via a custom GATT characteristic.

Extended protocol (aa06-aa0e) carries model metadata (CRC, shapes, flash
address, edge-learning flags) so the firmware can store everything in LittleFS
and program Akida from the correct flash slot.
*/

#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/types.h>

/* Event bits */
#define BUF_EVENT_FREE BIT(0) /* buffer is free  — camera can proceed */
#define BUF_EVENT_BUSY BIT(1) /* model update in progress — camera waits */

/* Shared event used to synchronize access to the SRAM buffer between threads */
extern struct k_event sram_buf_event;

/* Maximum number of shape dimensions carried over BLE */
#define MAX_MODEL_INP_SHAPE_DIMS 3
#define MAX_MODEL_OUTP_SHAPE_DIMS 3
/* Maximum size of the program_info binary stored in LittleFS */
#define MAX_MODEL_INFO_SIZE 1024

/**
 * @brief Model metadata header stored in LittleFS and received via BLE.
 *
 * Stored in two separate LittleFS files per app slot:
 *   File 1 (/ext/model_hdr_N)  – this struct, sizeof(model_meta_t) bytes
 *   File 2 (/ext/model_info_N) – raw program_info binary (info_data_len bytes)
 *
 * model_info_hdr_crc32 = CRC32( struct_bytes[total_length..info_data_len]
 *                             || program_info_bytes )
 * i.e. covers all fields except model_info_hdr_crc32 itself, followed by the
 * raw info binary.  Verified at transfer time and at boot by
 * file_transfer_load_meta().
 *
 * num_edge_classes packing (edge-learning models only):
 *   bits[31:16] = neurons_per_class
 *   bits[15:0]  = num_classes_to_learn
 *
 * All multi-byte fields are little-endian (native on Cortex-M).
 */
typedef struct {
  uint32_t model_info_hdr_crc32; /**< CRC32(hdr[total_length..info_data_len] ||
                                    info_bytes) */
  uint32_t total_length;     /**< (sizeof(model_meta_t)-8) + info_data_len */
  uint32_t input_shape[3];   /**< e.g. {49, 10, 1}, zero-padded             */
  uint32_t output_shape[3];  /**< e.g. {10, 1}, zero-padded               */
  uint32_t flash_address;    /**< SPI flash addr for data                 */
  uint32_t is_edge_learned;  /**< 1 = edge-learning model                 */
  uint32_t num_edge_classes; /**< upper16=neurons, lower16=classes        */
  uint32_t info_data_len;    /**< bytes of program_info                   */
} model_meta_t;

#define SRAM_BUFFER_SIZE CONFIG_SRAM_BUFFER_SIZE
/**
 * @brief External reference to the SRAM upload buffer used for DATA transfers.
 */
extern uint8_t sram_upload_buffer[];

/**
 * @brief Initialise the file transfer BLE service.
 * @return 0 on success, negative errno on failure.
 */
int file_transfer_init(void);

/* Initializes the SRAM buffer event and sets the buffer state to FREE */
void shared_buf_init(void);

/**
 * @brief Load model metadata from LittleFS.
 *
 * Reads two files for @p app_idx:
 *   1. Header file  – model_meta_t struct (CRC, shapes, flash_address…)
 *   2. Info file    – raw program_info binary
 *
 * On success the program_info bytes are placed in @c sram_upload_buffer and
 * the model_info_hdr_crc32 is verified (CRC over header fields + info bytes).
 *
 * Typical caller pattern after return 0:
 * @code
 *   akida_program_flash(sram_upload_buffer,
 *                       (int)meta_out->info_data_len,
 *                       meta_out->flash_address);
 * @endcode
 *
 * @param app_idx   Application slot (0 = MNIST, 1 = KWS / KWS_EL).
 * @param meta_out  Receives the model_meta_t header fields.
 * @return  0  success – @p meta_out valid, sram_upload_buffer ready.
 *          1  header file not found – use compiled defaults.
 *         -1  read error or CRC mismatch – use compiled defaults.
 */
int file_transfer_load_meta(int app_idx, model_meta_t *meta_out);

#endif /* FILE_TRANSFER_H_ */
