#ifndef INFER_UTILS_H
#define INFER_UTILS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Per-class max pooling over a dequantized float output.
 * For each class, selects the highest value across its num_neurons neurons
 * and writes it into class_maxes[c].
 *
 * @param output       Dequantized float buffer [num_classes * num_neurons]
 * @param num_classes  Number of output classes
 * @param num_neurons  Number of neurons per class
 * @param class_maxes  Output buffer [num_classes], caller-allocated
 */
void compute_per_class_max(const float *output, int num_classes,
                           int num_neurons, float *class_maxes);

/**
 * Numerically-stable in-place softmax over input[len].
 * Subtracts the maximum value before exponentiation to prevent overflow.
 * Normalizes the result to sum to 1.0.
 *
 * @param input  Array of logits, overwritten with probabilities [len]
 * @param len    Number of elements
 */
void softmax(float *input, uint32_t len);

#ifdef __cplusplus
}

/**
 * Argmax over a flat output array with num_neurons neurons per class.
 * Returns the flat index of the highest-valued element.
 * Divide by num_neurons to get the class index.
 *
 * @param softmax_output  Output array [num_classes * num_neurons]
 * @param num_classes     Number of output classes
 * @param num_neurons     Number of neurons per class
 * @return Flat index of the maximum value
 */
template <typename T>
uint32_t predict_class(T softmax_output[], uint32_t num_classes,
                       uint32_t num_neurons) {
  uint32_t predicted_index = 0;
  uint32_t n_activations = num_classes * num_neurons;

  /* Initialise max_prob with the first element of the array */
  T max_prob = softmax_output[0];

  for (uint32_t i = 1; i < n_activations; i++) {
    if (softmax_output[i] > max_prob) {
      max_prob = softmax_output[i];
      predicted_index = i;
    }
  }
  return predicted_index;
}

#endif /* __cplusplus */

#endif /* INFER_UTILS_H */
