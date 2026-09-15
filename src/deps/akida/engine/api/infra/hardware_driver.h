#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace akida {

class BlockDevice {
 public:
  virtual ~BlockDevice() = default;
  /**
   * @brief read operation.
   * @param address: address where data should be read
   * @param data: pointer data that will store the result
   * @param size: size data to be read
   */
  virtual void read(uint32_t address, void* data, size_t size) const = 0;

  /**
   * @brief read operation.
   * @param address: address where data should be read
   */
  uint32_t read32(uint32_t address) const {
    uint32_t ret;
    read(address, &ret, sizeof(uint32_t));
    return ret;
  }

  /**
   * @brief write operation
   * @param address: address where data should be written
   * @param data: pointer data to be written
   * @param size: data size in number of 32 bit words
   */
  virtual void write(uint32_t address, const void* data, size_t size) = 0;

  /**
   * @brief write operation
   * @param address: address where data should be written
   * @param data: uint32_t data value to be written
   */
  void write32(uint32_t address, const uint32_t data) {
    write(address, &data, sizeof(uint32_t));
  }
};

class HardwareDriver : public BlockDevice {
 public:
  /**
   * @brief Return a null terminated string with driver description.
   */
  virtual const char* desc() const = 0;

  /**
   * @brief Return address used for scratch memory.
   */
  virtual uint32_t scratch_memory() const = 0;

  /**
   * @brief Return size (in bytes) available as scratch memory.
   */
  virtual uint32_t scratch_size() const = 0;

  /**
   * @brief Return address used for top level registers.
   */
  virtual uint32_t top_level_reg() const = 0;

  /**
   * @brief return the address of data that are directly accessible by akida
   */
  virtual uint32_t akida_visible_memory() const = 0;
  /**
   * @brief return the size (in bytes) of data that are directly accessible by
   * akida
   */
  virtual uint32_t akida_visible_memory_size() const = 0;
};

}  // namespace akida
