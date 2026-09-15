#pragma once

#include <cstddef>
#include <cstdint>

#include "akida/program_info.h"
#include "infra/exports.h"

namespace akida {

/**
 * @brief Return the number of bytes required to store 1 input for the given
 * program
 * @param program_info: a ProgramInfo object built with the program whose input
 * size will be requested
 */
AKIDASHAREDLIB_EXPORT
size_t input_memory_required(const ProgramInfo& program_info);

/**
 * @brief Return the number of bytes required to store 1 output for the given
 * program
 * @param program_info: a ProgramInfo object built with the program whose output
 * size will be requested
 */
AKIDASHAREDLIB_EXPORT
size_t output_memory_required(const ProgramInfo& program_info);

/**
 * @brief Return the number of bytes required for 1 input descriptor for the
 * given program
 * @param program_info: a ProgramInfo object built with the program whose
 * required input dma size will be requested
 */
AKIDASHAREDLIB_EXPORT
size_t input_descriptor_memory_required(const ProgramInfo& program_info);

/**
 * @brief Return the number of bytes required for program descriptors
 * @param program_info: a ProgramInfo object built with the program whose
 * descriptors required size will be requested
 */
AKIDASHAREDLIB_EXPORT
size_t program_descriptors_memory_required(const ProgramInfo& program_info);

/**
 * @brief Return the number of bytes required for program
 * @param program_info: a ProgramInfo object built with the program whose size
 * will be requested
 */
AKIDASHAREDLIB_EXPORT
size_t program_data_memory_required(const ProgramInfo& program_info);

/**
 * @brief Return the number of bytes required for extra program data
 * @param program_info: a ProgramInfo object built with the program whose extra
 * data size will be requested
 */
AKIDASHAREDLIB_EXPORT
size_t extra_program_memory_required(const ProgramInfo& program_info);

}  // namespace akida
