
#ifndef AKIDA_MNIST_INPUTS_DATA_H_
#define AKIDA_MNIST_INPUTS_DATA_H_

#include <cstdint>
#include "akida/shape.h"
#include "akida/tensor.h"
extern const unsigned char mnist_inputs[];
extern const int64_t mnist_inputs_len;
extern const akida::Shape mnist_inputs_shape;
extern const akida::TensorType mnist_inputs_type;

#endif  // AKIDA_MNIST_INPUTS_DATA_H_
