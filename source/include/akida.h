#ifndef __AKIDA_H__
#define __AKIDA_H__
#include <stdint.h>

/**
 * Enable or Disable DMA clock counter
 *
 * @param enable to enable or disable DMA clock counter
 */
void akida_toggle_clock_counter(bool enable);

/**
 * Read clock from DMA clock counters
 *
 * @return DMA clock counter value
 */
uint32_t akida_get_clock_counter();

/**
 * Program the model into the mesh network and set learning mode.
 *
 * @param programm The program data to load
 * @param len length of the program data
 * @param learn_en enable the learning mode if true
 *
 * @return error code in case of failure else SUCCESS
 */
int akida_program(uint8_t *programm, int len, bool learn_en);

/**
 * Program the model into the mesh network.
 *
 * @param programm The program data to load
 * @param len length of the program data
 *
 * @return error code in case of failure else SUCCESS
 */
int akida_program_only(uint8_t *programm, int len);

/**
 * Program the model into the mesh network, if it is already available in device
 * accessible memory.
 *
 * @param program_info instance of the ProgramInfo, which is available in Mesh's
 * memory
 * @param len length of the program data
 * @param flash_address address of the program data
 * @param is_el_model is model edge learn capable
 *
 * @return error code in case of failure else SUCCESS
 */

int akida_program_flash(uint8_t *program_info, int len, uint32_t flash_address,
                        uint8_t *is_el_model);

/**
 * Set input batch size.
 *
 * @param size batch size
 * @param allocate_inputs allocates memory for the inputs if value is true
 *
 * @return effective batch size configured in mesh, might be lower than what was
 * requested
 */
int akida_batch_size(int size, bool allocate_inputs);
/**
 * Execute an inference on input data pointed by input pointer argument by
 * calling forward function of HardwareDeviceImpl class and store the float
 * output data at address pointed by output pointer argument.
 *
 * @param input the input data to send to inference
 * @param input_dims the input data dimensions will hold the size of the input
 * data
 * @param output the buffer where output data are stored
 * @param output_size the size of the output buffer
 *
 * @return SUCCESS if expected output size matches else error code
 */
int akida_forward(uint8_t *input, uint32_t *input_dims, uint8_t *output,
                  int output_size);

/**
 * Learn input
 *
 * @param input the input data to send to inference learn
 * @param input_dims the input data dimensions
 * @param input_label the class where the input should belong
 *
 */
void akida_fit(uint8_t *input, uint32_t *input_dims, int32_t *input_label);

/**
 * Enqueues input to akida
 *
 * @param input the input data to send to inference learn
 * @param input_dims the input data dimensions
 * @param input_label the class where the input should belong, applicable if
 * learning is enabled
 *
 * @return SUCCESS if no error occurred else -EAGAIN for application to retry
 */
int akida_enqueue(uint8_t *input, uint32_t *input_dims, int32_t *input_label);

/**
 * Function to fetch output from akida
 *
 * @param output the buffer where output data are stored
 * @param output_size the size of the output buffer
 * @param dequantize flag to decide if output need to be de-quantized
 *
 * @return SUCCESS if no error occurred else -EAGAIN for the application to
 * retry
 */
int akida_fetch(uint8_t *output, int output_size, bool dequantize);

/**
 * Toggle learn mode.
 *
 * @param enable enable or disable learning
 *
 * @return SUCCESS if learn mode is set else returns error code
 */
int akida_learn_mode(bool enable);

/**
 * If the memory size of the learned weights is less than or equal to the passed
 * size, then write a copy of the learn memory of current program in the given
 * buffer address and return the learn memory size. Otherwise, return EFAILURE.
 *
 * @param weights_ptr pointer to save the learned weights
 * @param size buffer size in bytes
 *
 * @return learned weights memory size in bytes or EFAILURE if passed buffer is
 * not large enough
 */
int akida_save_learn_weights(uint32_t *weights_ptr, uint32_t size);

/**
 *
 * Update the learned memory from the buffer containing a previously saved one
 * into the mesh if the size of the learned weight's memory is equal to the
 * passed size and return the learn memory size. Otherwise, return EFAILURE.
 *
 * @param weights_ptr pointer to learned weights
 * @param size buffer size in bytes
 *
 * @return learned weights memory size in bytes or EFAILURE if passed buffer is
 * not large enough
 */
int akida_update_learn_weights(const uint32_t *weights_ptr, uint32_t size);

/**
 * This function returns the size of the learned weights memory. To ensure safer
 * application execution when calling akida_save_learn_weights or
 * akida_update_learn_weights, the user should allocate memory in the
 * application that matches the returned size if not more.
 *
 * @return learned weights memory size in bytes
 */
uint32_t akida_learn_mem_size(void);

int32_t get_inferred_class(int32_t *result, int num_classes, int num_neurons);

#endif //__AKIDA_H__