#ifndef EDGE_AI_CAPTURE_H
#define EDGE_AI_CAPTURE_H

#include <stdbool.h>

#include "imu.h"

#define CAPTURE_WINDOW_SIZE    256
#define CAPTURE_PREBUFFER_SIZE 16

/* Pushes a sample into the circular pre-buffer, to be called in IDLE state
 * Keeps the last CAPTURE_PREBUFFER_SIZE samples to preserve gesture attack
 */
void capture_prebuffer_push(const struct imu_sample *sample);

/* Copies the pre-buffer content at the head of the capture window
 * Must be called when the trigger fires, before feeding new samples
 */
void capture_begin(void);

/* Returns the internal capture window, layout-compatible with
 * float[CAPTURE_WINDOW_SIZE][6] in the ax,ay,az,gx,gy,gz order
 */
const struct imu_sample *capture_window(void);

/* Appends one sample to the capture window
 * Returns true when the window is full (CAPTURE_WINDOW_SIZE samples)
 */
bool capture_feed(const struct imu_sample *sample);

/* Dumps the full window as CSV over the console, framed by BEGIN/END markers
 * Line format: ax,ay,az,gx,gy,gz
 */
void capture_dump_csv(void);

#endif /* EDGE_AI_CAPTURE_H */
