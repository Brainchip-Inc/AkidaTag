#include "akd1500/akd1500_spi_driver.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "akd1500/spi_flash.h"

namespace akida {

namespace akd1500::spi {

constexpr uint32_t kSlaveID = 0;

enum class Commands : uint8_t {
  Read = 0x60,
  Write = 0x80,
};

enum class BurstWordSize : uint8_t {
  x1 = 0,
  x4 = 1,
  x8 = 2,
  x16 = 3,
  x32 = 4,
};

}  // namespace akd1500::spi

Akd1500SpiDriver::Akd1500SpiDriver(AbstractSpiDriver* spi_driver,
                                   uint32_t akida_visible_memory_base,
                                   uint32_t akida_visible_memory_size)
    : spi_driver_(spi_driver),
      akida_visible_memory_base_(akida_visible_memory_base),
      akida_visible_memory_size_(akida_visible_memory_size) {
  // make sure we deassert line, or this could cause issues
  spi_driver_->chip_select(akd1500::spi::kSlaveID, false);

  // Init spi flash
  akd1500::init_spi_flash(this);
}

static inline uint32_t to_spi_address(const uint32_t address) {
  // SPI addresses are 24 bits: it can only access 32 bits aligned addresses in
  // 0xfcxxxxxx range.
  // To convert a 24 bits address to a 32 bits one, AKD1500 SPI slave left
  // shifts by 2 then adds 0xfc000000.
  // To do the opposite action we can substract 0xfc000000 then right shift
  // by 2. This is equivalent to right shift by 2, then discard the most
  // significant byte
  assert((address >> 24) == 0xfc &&
         "SPI Slave can only access 0xfcxxxxxx memory space");
  assert((address & 0b11) == 0 &&
         "SPI Slave can only access 32 bits aligned addresses");
  return (address >> 2u) & 0x00ffffff;
}

template<int burst_word_size>
constexpr akd1500::spi::BurstWordSize data_word_count_from_burst() {
  if constexpr (burst_word_size == 32) {
    return akd1500::spi::BurstWordSize::x32;
  } else if constexpr (burst_word_size == 16) {
    return akd1500::spi::BurstWordSize::x16;
  } else if constexpr (burst_word_size == 8) {
    return akd1500::spi::BurstWordSize::x8;
  } else if constexpr (burst_word_size == 4) {
    return akd1500::spi::BurstWordSize::x4;
  } else if constexpr (burst_word_size == 1) {
    return akd1500::spi::BurstWordSize::x1;
  }
}

void spi_header(akd1500::spi::Commands command,
                akd1500::spi::BurstWordSize word_count, const uint32_t address,
                uint8_t* buffer) {
  // 1st byte is command + burst size
  buffer[0] = static_cast<uint8_t>(command) | static_cast<uint8_t>(word_count);
  // next 3 bytes are address, MSB first
  const auto spi_address = to_spi_address(address);
  buffer[1] = (spi_address >> 16) & 0xff;
  buffer[2] = (spi_address >> 8) & 0xff;
  buffer[3] = spi_address & 0xff;
}

template<int word_size>
inline constexpr size_t words_to_bytes_size() {
  return word_size * sizeof(uint32_t);  // SPI uses 32 bits words unit
}

template<int burst_word_size>
static void spi_write_burst(AbstractSpiDriver* driver, uint32_t address,
                            const uint32_t* data) {
  // toggle slave line ON
  driver->chip_select(akd1500::spi::kSlaveID, true);

  // when writing, we put the header in the same buffer as the data to perform
  // a single write, so we have 1 more word in the burst data
  std::array<uint32_t, burst_word_size + 1> burst_data;
  auto* u8_data = reinterpret_cast<uint8_t*>(burst_data.data());
  spi_header(akd1500::spi::Commands::Write,
             data_word_count_from_burst<burst_word_size>(), address, u8_data);

  // now we perform the bytes swap
  for (size_t i = 0; i < burst_word_size; ++i) {
    burst_data[i + 1] = __builtin_bswap32(data[i]);
  }

  // then write all at once
  driver->write(u8_data, burst_data.size() * sizeof(uint32_t));

  // toggle slave line OFF
  driver->chip_select(akd1500::spi::kSlaveID, false);
}

template<int burst_word_size>
static void spi_read_burst(AbstractSpiDriver* driver, uint32_t address,
                           uint32_t* data) {
  // toggle slave line ON
  driver->chip_select(akd1500::spi::kSlaveID, true);

  // write header
  std::array<uint8_t, 5> header;
  spi_header(akd1500::spi::Commands::Read,
             data_word_count_from_burst<burst_word_size>(), address,
             header.data());
  header[4] = 0;  // read requires to wait 8 spi clocks before response, so we
                  // insert a dummy byte that will delay the read accordingly
  driver->write(header.data(), header.size());
  std::array<uint32_t, burst_word_size> burst_data;
  driver->read(reinterpret_cast<uint8_t*>(burst_data.data()),
               burst_data.size() * sizeof(uint32_t));

  // now we perform the bytes swap
  for (size_t i = 0; i < burst_word_size; ++i) {
    data[i] = __builtin_bswap32(burst_data[i]);
  }

  // toggle slave line OFF
  driver->chip_select(akd1500::spi::kSlaveID, false);
}

template<akd1500::spi::Commands command, int burst_word_size, typename buffer>
static inline void loop_bursts(AbstractSpiDriver* driver, uint32_t* address,
                               buffer* data, size_t* word_size) {
  // just loop until we cannot burst with the remaining size
  while (*word_size >= burst_word_size) {
    if constexpr (command == akd1500::spi::Commands::Write) {
      spi_write_burst<burst_word_size>(driver, *address, *data);
    } else {
      spi_read_burst<burst_word_size>(driver, *address, *data);
    }
    *word_size -= burst_word_size;
    *address += static_cast<uint32_t>(words_to_bytes_size<burst_word_size>());
    *data += burst_word_size;
  }
}

template<akd1500::spi::Commands command, typename buffer>
static inline void spi_op(AbstractSpiDriver* driver, uint32_t address,
                          buffer data, size_t word_size) {
  // buffer is template, but it is used only to have the same code to both const
  // uint32_t* and uint32_t* variants
  static_assert(
      std::is_same<typename std::remove_const<
                       typename std::remove_pointer<buffer>::type>::type,
                   uint32_t>::value,
      "buffer type should be uint32_t");
  // read or write using the highest burst size until there is no data
  loop_bursts<command, 32>(driver, &address, &data, &word_size);
  loop_bursts<command, 16>(driver, &address, &data, &word_size);
  loop_bursts<command, 8>(driver, &address, &data, &word_size);
  loop_bursts<command, 4>(driver, &address, &data, &word_size);
  loop_bursts<command, 1>(driver, &address, &data, &word_size);
}

void Akd1500SpiDriver::read(uint32_t address, void* data, size_t size) const {
  // AKD1500 SPI have transfers aligned to 32 bits
  const auto nb_32b_words = size / sizeof(uint32_t);
  const auto unaligned_bytes = size % sizeof(uint32_t);

  // Read full words
  spi_op<akd1500::spi::Commands::Read>(
      spi_driver_, address, reinterpret_cast<uint32_t*>(data), nb_32b_words);

  // then handle non aligned bytes
  if (unaligned_bytes > 0) {
    uint32_t word;
    // read a full word
    spi_read_burst<1>(
        spi_driver_,
        address + static_cast<uint32_t>(nb_32b_words * sizeof(uint32_t)),
        &word);

    // then put relevant data at the correct location in the bytes buffer
    auto* u8_data = reinterpret_cast<uint8_t*>(data);
    if (unaligned_bytes == 1) {
      u8_data[size - 1] = static_cast<uint8_t>(word & 0xFF);
    } else if (unaligned_bytes == 2) {
      u8_data[size - 1] = static_cast<uint8_t>((word >> 8) & 0xFF);
      u8_data[size - 2] = static_cast<uint8_t>(word & 0xFF);
    } else if (unaligned_bytes == 3) {
      u8_data[size - 1] = static_cast<uint8_t>((word >> 16) & 0xFF);
      u8_data[size - 2] = static_cast<uint8_t>((word >> 8) & 0xFF);
      u8_data[size - 3] = static_cast<uint8_t>(word & 0xFF);
    }
  }
}

void Akd1500SpiDriver::write(uint32_t address, const void* data, size_t size) {
  // AKD1500 SPI have transfers aligned to 32 bits
  const auto nb_32b_words = size / sizeof(uint32_t);
  const auto unaligned_bytes = size % sizeof(uint32_t);

  // transfer full 32 bits words (they will get bytes swapped during spi burst)
  spi_op<akd1500::spi::Commands::Write>(spi_driver_, address,
                                        reinterpret_cast<const uint32_t*>(data),
                                        nb_32b_words);
  // then handle non aligned bytes
  if (unaligned_bytes > 0) {
    uint32_t word;
    // write a full word from unaligned data
    const auto* u8_data = reinterpret_cast<const uint8_t*>(data);
    if (unaligned_bytes == 1) {
      word = static_cast<uint32_t>(u8_data[size - 1]);
    } else if (unaligned_bytes == 2) {
      word =
          static_cast<uint32_t>(u8_data[size - 2] | (u8_data[size - 1] << 8));
    } else if (unaligned_bytes == 3) {
      word =
          static_cast<uint32_t>(u8_data[size - 3] | (u8_data[size - 2] << 8) |
                                (u8_data[size - 1] << 16));
    }
    spi_write_burst<1>(
        spi_driver_,
        address + static_cast<uint32_t>(nb_32b_words * sizeof(uint32_t)),
        &word);
  }
}
}  // namespace akida
