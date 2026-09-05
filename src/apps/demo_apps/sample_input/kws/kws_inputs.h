
#ifndef AKIDA_KWS_INPUTS_DATA_H_
#define AKIDA_KWS_INPUTS_DATA_H_

#include "akida/shape.h"
#include "akida/tensor.h"
#include <cstdint>
extern const unsigned char kws_inputs[];
extern const int64_t kws_inputs_len;
extern const akida::Shape kws_inputs_shape;
extern const akida::TensorType kws_inputs_type;

extern const char *const kws_tags[];
extern const char *const kws_new_tags[];
extern const int kws_new_tags_count;

#endif // AKIDA_KWS_INPUTS_DATA_H_
