#ifndef INFERENCE_H_
#define INFERENCE_H_

#include <stdint.h>
#include <stdbool.h>

#define CAPTURE_SAMPLES 256
#define IMU_CHANNELS    6

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float prob_A;
    float prob_IDLE;
    int predicted_class; /* 0 = A, 1 = IDLE */
} inference_result_t;

int inference_init(void);
int inference_run(const float raw_data[CAPTURE_SAMPLES][IMU_CHANNELS], inference_result_t *result);

/* Cycles spent in the last inference, as counted by the DWT cycle
 * counter (CPU clock: 96 MHz)
 */
uint32_t inference_last_cycles(void);

#ifdef __cplusplus
}
#endif

#endif /* INFERENCE_H_ */