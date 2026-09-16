#ifndef FILE_TRANSFER_H_
#define FILE_TRANSFER_H_

/*
Model Transfer Service receives a model from a central device (e.g., phone) over
a custom GATT service, one flash sector at a time.

The wire protocol is specified in full elsewhere; what a reader of this header
needs is the shape. Every chunk written to the data characteristic carries the
absolute offset it belongs at, the firmware stages one MODEL_TRANSFER_BLOCK_SIZE
block, commits it to its destination, verifies it by reading it back, and
notifies the position it has committed. The whole file is checked against a
CRC32 the host supplied before anything is trusted.

Metadata characteristics (aa05-aa12) carry the CRC, shapes, flash address and
edge-learning flags so the firmware can store everything in LittleFS and program
Akida from the correct flash slot.
*/

#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/types.h>

/* Maximum number of shape dimensions carried over BLE */
#define MAX_MODEL_INP_SHAPE_DIMS 3
#define MAX_MODEL_OUTP_SHAPE_DIMS 3
/* Maximum size of the program_info binary stored in LittleFS */
#define MAX_MODEL_INFO_SIZE 2300
/* Maximum length of the model name string (including null terminator) */
#define MAX_FS_NAME_LEN 64

/**
 * @brief Model metadata header stored in LittleFS and received via BLE.
 *
 * Stored in two separate LittleFS files per app slot:
 *   File 1 (/ext/model_hdr_N)  – this struct, sizeof(model_meta_t) bytes
 *   File 2 (/ext/model_info_N) – raw program_info binary (info_data_len bytes)
 *
 * model_info_hdr_crc32 = CRC32( struct_bytes[total_length..model_name]
 *                             || program_info_bytes )
 * i.e. covers all fields except model_info_hdr_crc32 itself (total_length
 * through model_name inclusive), followed by the raw info binary.
 * Verified at transfer time and at boot by file_transfer_load_meta().
 *
 * num_edge_classes packing (edge-learning models only):
 *   bits[31:16] = neurons_per_class
 *   bits[15:0]  = num_classes_to_learn
 *
 * All uint32_t fields are little-endian (native on Cortex-M).
 * Total size: 64 bytes (uint32 fields) + 64 bytes (model_name) = 128 bytes.
 */
typedef struct {
    uint32_t model_info_hdr_crc32;    /**< CRC32(hdr[total_length..model_name] ||
                                         info_bytes) */
    uint32_t total_length;            /**< program_info size + program_data size (bytes) */
    uint32_t input_shape[3];          /**< e.g. {49, 10, 1}, zero-padded             */
    uint32_t output_shape[3];         /**< e.g. {10, 1}, zero-padded               */
    uint32_t flash_address;           /**< SPI flash addr for data                 */
    uint32_t is_edge_learned;         /**< 1 = edge-learning model                 */
    uint32_t num_edge_classes;        /**< upper16=neurons, lower16=classes        */
    uint32_t info_data_len;           /**< bytes of program_info                   */
    uint32_t mfcc_fs_bits;            /**< IEEE-754 bits of MFCC normalisation scalar */
    uint32_t silence_class;           /**< Output index of silence class           */
    uint32_t unknown_class;           /**< Output index of unknown/garbage class   */
    uint32_t inference_mode;          /**< 0 = sync, 1 = async (from info.yaml)    */
    char model_name[MAX_FS_NAME_LEN]; /**< Model name, e.g. "kws"        */
} model_meta_t;

/**
 * @brief Model data metadata stored in LittleFS (3rd file per app slot).
 *
 * Written at the end of a successful DATA BLE transfer.
 * Read at boot to validate SPI flash contents before programming Akida.
 *
 * File path: /ext/{model_name}_model_data_hdr
 *
 * data_crc32 = CRC32(all model_data.bin bytes), using Zephyr crc32_ieee
 *              with init 0xFFFFFFFF and final XOR 0xFFFFFFFF.
 * first_4_bytes = raw 4 bytes at the start of model_data.bin (as stored in
 *                 flash, before any endian swap).
 */
typedef struct {
    uint32_t data_crc32;    /**< CRC32 of complete model_data.bin   */
    uint32_t first_4_bytes; /**< First 4 bytes as stored in flash   */
    uint32_t data_length;   /**< Total size in bytes of model_data  */
} model_data_meta_t;

/**
 * @brief Bytes held in RAM at once during a transfer: one SPI flash sector.
 *
 * Also the unit the host acknowledges at, and the granularity of the erase,
 * program and read-back cycle. Reported to the host in every status
 * notification so it never has to hard-code the value.
 */
#define MODEL_TRANSFER_BLOCK_SIZE 4096u

/**
 * @brief Staging buffer for one block, and the scratch every other operation in
 *        this module borrows.
 *
 * After file_transfer_load_meta() returns 0 it holds the program_info binary,
 * ready to pass to akida_program_flash(). file_transfer_validate_flash_data()
 * and an in-flight transfer both overwrite it.
 */
extern uint8_t model_transfer_buffer[];

/**
 * @brief Initialise the file transfer BLE service.
 * @return 0 on success, negative errno on failure.
 */
int file_transfer_init(void);

/**
 * @brief Load model metadata from LittleFS.
 *
 * Reads two files for @p app_idx:
 *   1. Header file  – model_meta_t struct (CRC, shapes, flash_address…)
 *   2. Info file    – raw program_info binary
 *
 * On success the program_info bytes are placed in @c model_transfer_buffer and
 * the model_info_hdr_crc32 is verified (CRC over header fields + info bytes).
 *
 * Typical caller pattern after return 0:
 * @code
 *   akida_program_flash(model_transfer_buffer,
 *                       (int)meta_out->info_data_len,
 *                       meta_out->flash_address);
 * @endcode
 *
 * @param app_idx   Application slot.
 * @param meta_out  Receives the model_meta_t header fields.
 * @return  0  success – @p meta_out valid, model_transfer_buffer ready.
 *          1  header file not found – use compiled defaults.
 *         -1  read error or CRC mismatch – use compiled defaults.
 */
int file_transfer_load_meta(int app_idx, model_meta_t* meta_out);

/**
 * @brief Read only the model_meta_t header from LittleFS (no
 * model_transfer_buffer usage).
 *
 * Lightweight variant of file_transfer_load_meta() — reads only the header
 * struct without loading program_info into model_transfer_buffer.  Use this
 * before file_transfer_validate_flash_data() so the buffer is not clobbered
 * before program_info is loaded.
 *
 * @param app_idx   Application slot.
 * @param meta_out  Receives the model_meta_t header (flash_address, etc.).
 * @return  0  success.
 *          1  header file not found.
 *         -1  read error.
 */
int file_transfer_read_meta_hdr_only(int app_idx, model_meta_t* meta_out);

/**
 * @brief Load model_data metadata from LittleFS (3rd file per slot).
 *
 * @param app_idx  Application slot.
 * @param dm_out   Receives the model_data_meta_t fields.
 * @return  0  success.
 *          1  file not found (no DATA upload for this slot yet).
 *         -1  read error.
 */
int file_transfer_load_data_meta(int app_idx, model_data_meta_t* dm_out);

/**
 * @brief Validate model data in SPI flash against stored model_data_meta_t.
 *
 * Reads dm->data_length bytes from SPI flash starting at flash_addr in
 * MODEL_TRANSFER_BLOCK_SIZE chunks via model_transfer_buffer, computes CRC32,
 * and checks:
 *   1. First 4 bytes match dm->first_4_bytes.
 *   2. CRC32 matches dm->data_crc32.
 *
 * WARNING: This function overwrites model_transfer_buffer.  After calling this,
 * reload program_info by calling file_transfer_load_meta() before
 * akida_program_flash().
 *
 * @param flash_addr  SPI flash offset where model_data begins.
 * @param dm          Stored model_data_meta_t to validate against.
 * @return  0  all checks pass.
 *         -1  mismatch or invalid parameters.
 */
int file_transfer_validate_flash_data(uint32_t flash_addr, const model_data_meta_t* dm);

/**
 * @brief Check that model_name is valid for the given app slot.
 *
 * Constructs "/ext/{model_name}_model_hdr" and compares it against the
 * hardcoded path for app_idx.
 *
 * @param app_idx     Application slot.
 * @param model_name  Name to validate (e.g. "kws").
 * @return  0  valid for this slot.
 *         -1  mismatch or unknown name.
 */
int file_transfer_check_model_name(int app_idx, const char* model_name);

#endif /* FILE_TRANSFER_H_ */
