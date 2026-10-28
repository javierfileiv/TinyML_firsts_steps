#include <zephyr/sys/printk.h>

#include "capture.h"

/* Circular pre-buffer preserving the gesture attack while in IDLE */
static struct imu_sample prebuffer[CAPTURE_PREBUFFER_SIZE];
static unsigned int prebuffer_head; /* next write position */
static unsigned int prebuffer_count; /* valid samples, capped at CAPTURE_PREBUFFER_SIZE */

/* Acquisition window, pre-filled with the pre-buffer then completed live */
static struct imu_sample window[CAPTURE_WINDOW_SIZE];
static unsigned int window_pos;

void capture_prebuffer_push(const struct imu_sample *sample)
{
	prebuffer[prebuffer_head] = *sample;
	prebuffer_head = (prebuffer_head + 1) % CAPTURE_PREBUFFER_SIZE;
	if (prebuffer_count < CAPTURE_PREBUFFER_SIZE) {
		prebuffer_count++;
	}
}

void capture_begin(void)
{
	unsigned int oldest = (prebuffer_head + CAPTURE_PREBUFFER_SIZE - prebuffer_count)
			      % CAPTURE_PREBUFFER_SIZE;

	for (unsigned int i = 0; i < prebuffer_count; i++) {
		window[i] = prebuffer[(oldest + i) % CAPTURE_PREBUFFER_SIZE];
	}
	window_pos = prebuffer_count;
}

bool capture_feed(const struct imu_sample *sample)
{
	if (window_pos >= CAPTURE_WINDOW_SIZE) {
		return true;
	}

	window[window_pos++] = *sample;

	return window_pos >= CAPTURE_WINDOW_SIZE;
}

void capture_dump_csv(void)
{
	printk("BEGIN\n");
	for (unsigned int i = 0; i < CAPTURE_WINDOW_SIZE; i++) {
		printk("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
		       (double)window[i].ax, (double)window[i].ay,
		       (double)window[i].az, (double)window[i].gx,
		       (double)window[i].gy, (double)window[i].gz);
	}
	printk("END\n");
}
