#pragma once

#include "dma_engine.h"

#include <cstdint>
#include <vector>

#include "akida/program_info.h"
#include "engine/dma.h"
#include "infra/hardware_driver.h"
#include "infra/registers_common.h"

#include "dma_desc_ops.h"

namespace akida {

namespace dma {
inline constexpr uint32_t MAX_PIPELINE_SIZE = 16;

/**
 * @brief Configure DMA descriptors buffer and number of descriptors.
 *
 * It tells the DMA where to look at for descriptors, and how many descriptors
 * it will loop on
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Engine object which must be configured
 * @param num_descriptors: number of descriptors used by the DMA. Must be > 1
 */
void configure_descriptors_buffer(HardwareDriver* driver, const Engine& dma,
                                  uint32_t num_descriptors);

/**
 * @brief Configure control register and enable/disable DMA engine
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param reg_base_addr: base address of dma registers
 * @param enabled: true to enable dma (running), false to disable it
 */
void toggle_engine(HardwareDriver* driver, uint32_t reg_base_addr,
                   bool enabled);

/**
 * @brief Enqueue a descriptor to be processed by DMA.
 *
 * It does the following:
 * - Copies descriptor to descriptors buffer in scratch buffer at the next
 *   available index
 * - Programs DMA to process it without waiting for completion, by incrementing
 *   DMA_LAST_DESC_CONT field from register DMA_DESC_CONT_REG
 *   DMA_LAST_DESC_CONT is a circular counter from 0 to the max number of
 *   descriptors - 1
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Engine object where descriptor will be enqueued
 * @param descriptor: dma::Descriptor object to enqueue
 *
 * @return The address where the descriptor was written
 */
dma::addr enqueue_descriptor(HardwareDriver* driver, const Engine& dma,
                             const dma::Descriptor& descriptor);

/**
 * @brief Tell config DMA engine to process a given descriptor.
 *
 * It does the following:
 * - Turns DMA on
 * - Calls enqueue_descriptor function (see its comment to know what this
 *   function is doing).
 * - Waits for descriptor to be processed
 * - Turns DMA off
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Config object where descriptor will be processed
 * @param descriptor: dma::Descriptor object to process
 * @param wait_for_completion: boolean to wait for the completion of process
 */
void process(HardwareDriver* driver, const Config& dma,
             const Descriptor& descriptor, bool wait_for_completion);

/**
 * @brief Used in single pass: return ID of last processed job
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Inputs object on which the last job id will be retrieved
 *
 * @return ID of last processed job read from dma
 */
uint16_t get_last_job_id_processed(HardwareDriver* driver, const Inputs& dma);

/**
 * @brief Turn clock counter measures on or off
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Engine object on which toggle clock counter
 * @param: enabled: true to enabled clock counter, false to disable it
 */
void toggle_buffer_timer(HardwareDriver* driver, const Engine& dma,
                         bool enabled);

/**
 * @brief Retrieve clock counter measures
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Engine object where the clock counter will be read
 *
 * @return value of clock counter register
 */
uint32_t read_buffer_timer(HardwareDriver* driver, const Engine& dma);

/**
 * @brief Tell if clock counter is enabled
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Inputs object to check if clock counter is enabled
 *
 * @return true if clock counter is enabled, false if it is disabled
 */
bool is_buffer_timer_enabled(const HardwareDriver& driver, const Inputs& dma);

/**
 * @brief Enable or disable pipeline.
 *
 * When enabled, it will be kept enable on best effort. It must be disabled in
 * multi pass and when learning is enabled.
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Inputs object to toggle pipeline mode
 * @param enabled: true if pipeline must enabled, false if it must be disabled
 */
void toggle_pipeline(HardwareDriver* driver, const Inputs& dma, bool enabled);

/**
 * @brief Configure input dma for multipass mode
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Inputs object to be configured
 * @param hw_desc_addr: address where HW generated descriptor will be written
 * @param hw_payload_addr: address where HW generated payload will be written
 * @param num_loops: number of passes
 */
void prepare_engine_multi_pass(HardwareDriver* driver, const Inputs& dma,
                               dma::addr hw_desc_addr,
                               dma::addr hw_payload_addr, uint32_t num_loops);

/**
 * @brief Configure output buffer clearing policy
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Inputs object ton configure
 * @param clear_size: Size (in bytes) that will be zeroed at each output. It
 * will be 32 bits aligned
 */
void set_output_buffer_clear(HardwareDriver* driver, const Inputs& dma,
                             uint32_t clear_size);

/**
 * @brief Check if the given interrupt (flag) is set on the DMA
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Engine object to check the interrupt
 * @param flag: the field (type of interrupt) to check
 *
 * @return true if the interrupt is set, false otherwise
 */
bool check_for_interrupt(HardwareDriver* driver, const Engine& dma,
                         const RegDetail& flag);

/**
 * @brief Clear all interrupts from the DMA
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Engine object to clear the interrupt
 */
void clear_interrupts(HardwareDriver* driver, const Engine& dma);

/**
 * @brief Configure config dma depending on the given program info.
 *
 * It does the following:
 * - Soft reset dma
 * - Configures descriptors buffer & number by calling
 *   configure_descriptors_buffer function
 * - Masks some interrupts depending on multipass mode or not
 * - Toggles multipass mode, if on, set the number of descriptors per pass
 * - Toggles output header (on for single pass, off for multipass)
 * If the program is multi pass, it also:
 * - Configures the number of extra descriptor required (extra descriptors are
 *   needed to learn using FNP3, to update weights from NP to program after each
 *   input)
 * - Configures outbound container size
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Config object to be configured
 * @param program_info: ProgramInfo object containing information to configure
 * the dma
 */
void init_config_dma(HardwareDriver* driver, const Config& dma,
                     const ProgramInfo& program_info);

/**
 * @brief Toggle extra descriptors.
 *
 * Extra descriptors are used by learning using FNP3, to update weights from NP
 * to program after each input
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Config object to toggle descriptors
 * @param enable: true if extra descriptors must be enabled, false if not
 */
void toggle_extra_descriptors(HardwareDriver* driver, const Config& dma,
                              bool enable);

/**
 * @brief Configure DMA with default values
 *
 * FIXME: this should not be called on config DMA, unless to scan mesh. When
 * scanning mesh will be out of the engine, this could be renamed to
 * init_input_dma and take const dma::Input& as argument
 * It does the following:
 * - Soft reset dma
 * - Configures descriptors buffer & number by calling
 *   configure_descriptors_buffer function
 * - Toggles output header on or off
 * - Toggles dma off
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Engine object to configure
 * @param number_of_descriptors: number of descriptors that will be used by dma
 * @param output_header: true if the dma must generate header on outputs, false
 * if no header
 */
void init_default_dma(HardwareDriver* driver, const Engine& dma,
                      uint32_t number_of_descriptors,
                      bool output_header = true);

/**
 * @brief Turn config DMA on, and wait for config DMA to configure NPs
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Config object to turn on
 */
void enable_config_dma_multipass(HardwareDriver* driver, const Config& dma);

/**
 * @brief Wait for config DMA to process a descriptor
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Config object to wait for
 */
void wait_config_dma_descriptor_complete(HardwareDriver* driver,
                                         const Config& dma);

/**
 * @brief Enqueue descriptor at "extra descriptors" location.
 *
 * This works if we have only 1 total extra descriptor, which is our use case,
 * because it is only used by learn multipass, and learning can't be split
 * accross NPs so there will be a single extra descriptor in a program
 *
 * @param driver: the HardwareDriver object used by the current device
 * @param dma: dma::Config object where the descriptor will be enqueued
 * @param descriptor: dma::Descriptor that will be enqueued
 */
void enqueue_extra_descriptor(HardwareDriver* driver, const Config& dma,
                              const Descriptor& descriptor);

}  // namespace dma

}  // namespace akida
