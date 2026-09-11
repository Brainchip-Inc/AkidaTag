#include "engine/dma_config_ops.h"

#include <cassert>

#include "dma_config_format.h"
#include "infra/registers_common.h"

namespace akida {
namespace dma {

static uint8_t target_to_uid(const Target& target) {
  switch (target) {
    case Target::CnpFilter:
      return HDR_UID_CNP_FILTER;
    case Target::CnpFilterCompact:
      return HDR_UID_CNP_FILTER_COMPACT;
    case Target::InputShifts:
      return HDR_UID_INPUT_SHIFT;
    case Target::CnpLearnThres:
      return HDR_UID_CNP_LEARN_THRES;
    case Target::CnpFireThres:
      return HDR_UID_CNP_THRES_FIRE;
    case Target::CnpBiasOutScale:
      return HDR_UID_CNP_BIAS_OUT_SCALES;
    case Target::FnpWeights:
      return HDR_UID_FNP_WEIGHT;
    case Target::NpRegisters:
    case Target::SkipDmaRegisters:
      return HDR_UID_NP_REGS;
    case Target::HrcRegisters:
      return HDR_UID_HRC_REGS;
    case Target::HrcSram:
      return HDR_UID_HRC_SRAM;
    case Target::IBPckSram:
      return HDR_UID_IB_PCK;
    case Target::LookUpTable:
      return HDR_UID_LUT;
    default:
      break;
  }
  // this should never be reached
  assert(false);
  return 0;
}

void format_config_header(uint32_t* header, const struct hw::Ident& np,
                          Target target, uint32_t size, uint16_t dest_addr,
                          bool xl) {
  // init header to 0
  std::memset(header, 0, kConfigNpHeaderByteSize);
  bool hrc_en = (np == hw::HRC_IDENT);

  assert(size > 0);
  assert(!xl || (xl && ((size & 0xF) == 0) &&
                 "size must be a multiple of 16 when using XL mode"));
  assert(xl || (!xl && !config_block_size_needs_xl(size) &&
                "DMA transfer size cannot fit without using XL mode"));

  set_field(&header[HDR_WORD1], HDR_NP_COL, np.col);
  set_field(&header[HDR_WORD1], HDR_NP_ROW, np.row);
  set_field(&header[HDR_WORD1], HDR_NP_DST, hrc_en ? 0x0 : 1u << np.id);
  set_field(&header[HDR_WORD1], HDR_HRC_EN, hrc_en ? 0b10 : 0b00);
  set_field(&header[HDR_WORD1], HDR_UID, target_to_uid(target));

  set_field(&header[HDR_WORD2], HDR_XL, xl ? 1 : 0);
  // if xl increment is set, calculate buffer size in 16 words
  if (xl) {
    size = size / kXlIncrementSz;
  }
  set_field(&header[HDR_WORD2], HDR_BLOCK_LEN, size);
  set_field(&header[HDR_WORD2], HDR_START_ADDR, dest_addr);
}

uint32_t parse_config_read_size(const dma::wbuffer& read_header) {
  return get_field(read_header[HDR_READ_WORD1], HDR_READ_PACKET_SZ);
}

bool config_block_size_needs_xl(uint32_t block_size) {
  return (block_size >= ((1u << HDR_BLOCK_LEN.nb_bits) - 1));
}

}  // namespace dma

}  // namespace akida
