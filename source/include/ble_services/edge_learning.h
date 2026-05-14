#ifndef EDGE_LEARNING_H_
#define EDGE_LEARNING_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/types.h>
/**
 * @brief Acknowledgement code sent when edge learning is started & completed.
 */
#define ACK_LEARNING_START 0xA6
#define ACK_LEARNING_DONE 0xA7

/**
 * @brief Process an edge learning command received from BLE.
 */
void edge_learning_cmd_process(uint8_t value);

/**
 * @brief Notify that edge learning has started & completed.
 */
void learning_completed(void);
void learning_started(void);
#endif /* EDGE_LEARNING_H_ */