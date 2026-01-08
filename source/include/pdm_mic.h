#ifndef PDM_MIC_H_
#define PDM_MIC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int dmic_rms_init(void);
int dmic_rms_start(void);
int dmic_rms_read(int16_t *rms_out);

#ifdef __cplusplus
}
#endif

#endif /* PDM_MIC_H_ */