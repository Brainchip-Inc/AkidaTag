#include "infer_utils.h"
#include <math.h>
#include <stddef.h>

void compute_per_class_max(const float *output, int num_classes,
                           int num_neurons, float *class_maxes) {
  for (int c = 0; c < num_classes; c++) {
    float max_val = output[c * num_neurons];
    for (int n = 1; n < num_neurons; n++) {
      float val = output[c * num_neurons + n];
      if (val > max_val)
        max_val = val;
    }
    class_maxes[c] = max_val;
  }
}

void softmax(float *input, uint32_t len) {
  if (len == 0 || input == NULL)
    return;

  /* Find maximum for numerical stability */
  float max_val = input[0];
  for (uint32_t i = 1; i < len; i++) {
    if (input[i] > max_val)
      max_val = input[i];
  }

  /* Subtract max, exponentiate, and accumulate sum */
  float sum = 0.0f;
  for (uint32_t i = 0; i < len; i++) {
    input[i] = expf(input[i] - max_val);
    sum += input[i];
  }

  /* Prevent divide-by-zero: fall back to uniform distribution */
  if (sum == 0.0f) {
    float uniform = 1.0f / (float)len;
    for (uint32_t i = 0; i < len; i++) {
      input[i] = uniform;
    }
    return;
  }

  /* Normalize to probabilities */
  float inv_sum = 1.0f / sum;
  for (uint32_t i = 0; i < len; i++) {
    input[i] *= inv_sum;
  }
}
