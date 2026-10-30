#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include "imu.h"
#include "capture.h"
#include "dfu_boot.h"
#include "inference.h"

#define COOLDOWN_MS 500

/* Automatic gyro-energy trigger: disable it during dataset collection
 * so captures only start on the button or the console command; flip
 * to 1 for threshold calibration and autonomous inference (Phase 3)
 */
#define ENABLE_AUTO_TRIGGER 0

/* Angular kinetic energy trigger, in (rad/s)^2
 * 12.0 is roughly 115 dps on each axis combined, tune after first captures
 */
#define GYRO_ENERGY_THRESHOLD 12.0f

/* Minimum confidence to declare a gesture A recognized (Phase 3 spec) */
#define CONFIDENCE_THRESHOLD 0.8f

/* Which inference kernel set this build runs: the timing lines say
 * which engine produced each number
 */
#ifdef CONFIG_TENSORFLOW_LITE_MICRO_CMSIS_NN_KERNELS
#define KERNEL_TAG "cmsis_nn"
#else
#define KERNEL_TAG "reference"
#endif

enum app_state {
	STATE_IDLE,
	STATE_ACQUIRE,
	STATE_COOLDOWN,
};

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct gpio_callback button_cb_data;

K_SEM_DEFINE(button_sem, 0, 1);
K_SEM_DEFINE(sample_sem, 0, 4);

static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_sem_give(&button_sem);
}

/* Given by the MPU6050 data-ready interrupt at exactly 100 Hz */
static void data_ready_handler(void)
{
	k_sem_give(&sample_sem);
}

static float gyro_energy(const struct imu_sample *sample)
{
	return sample->gx * sample->gx
	       + sample->gy * sample->gy
	       + sample->gz * sample->gz;
}

static int gpio_setup(void)
{
	int ret;

	if (!device_is_ready(button.port) || !device_is_ready(led.port)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	return gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

int main(void)
{
	struct imu_sample sample;
	enum app_state state = STATE_IDLE;
	int64_t cooldown_end = 0;
	/* CSV dump on by default (data collection); 'w' on the console
	 * toggles watch mode: detections only, the final-product voice
	 */
	bool csv_logging = true;

	if (imu_init() < 0) {
		printk("MPU6050 not ready\n");
		return 0;
	}

	if (gpio_setup() < 0) {
		printk("GPIO setup failed\n");
		return 0;
	}

	if (imu_start(data_ready_handler) < 0) {
		printk("MPU6050 data-ready trigger failed\n");
		return 0;
	}

	if (inference_init() < 0) {
		printk("TFLM inference init failed\n");
		return 0;
	}

	printk("edge_ai data logger ready [dfu v5], 100 Hz, window %u samples\n",
	       CAPTURE_WINDOW_SIZE);

	while (1) {
		/* Check for the "dfu" console trigger at 100 Hz, the
		 * poll also returns the consumed character
		 */
		int c = dfu_boot_poll();

		/* Space from the console host starts a capture right
		 * away: the settle delay lives host-side in capture.py
		 */
		if (c == ' ') {
			if (state == STATE_IDLE) {
				capture_begin();
				gpio_pin_set_dt(&led, 1);
				state = STATE_ACQUIRE;
				printk("console capture started\n");
			} else {
				printk("console capture busy\n");
			}
		}

		/* 'w' toggles watch mode: with it on the board prints
		 * detections only and stays quiet about the samples
		 */
		if (c == 'w') {
			csv_logging = !csv_logging;
			printk("watch mode %s\n",
			       csv_logging ? "OFF (csv output)" :
					     "ON (detections only)");
		}

		/* 'p' benches the last captured window: five inferences
		 * through the DWT cycle counter, averaged
		 */
		if (c == 'p') {
			uint32_t total = 0;

			for (int i = 0; i < 5; i++) {
				inference_result_t r;

				if (inference_run(
					    (const float (*)[6])
						    capture_window(),
					    &r) != 0) {
					printk("bench: inference failed\n");
					break;
				}
				total += inference_last_cycles();
			}
			printk("bench: %u cycles avg over 5 runs (%u us) [%s]\n",
			       (unsigned)(total / 5U),
			       (unsigned)(total / 5U / 96U),
			       KERNEL_TAG);
		}

		/* Wait only for DRDY INT */
		if (k_sem_take(&sample_sem, K_MSEC(15)) != 0) {
			continue;
		}

		if (imu_read(&sample) < 0) {
			continue;
		}

		switch (state) {
		case STATE_IDLE:
			capture_prebuffer_push(&sample);
			if (k_sem_take(&button_sem, K_NO_WAIT) == 0
			    || (ENABLE_AUTO_TRIGGER &&
				gyro_energy(&sample) > GYRO_ENERGY_THRESHOLD)) {
				capture_begin();
				gpio_pin_set_dt(&led, 1);
				state = STATE_ACQUIRE;
				printk("capture started\n");
			}
			break;

		case STATE_ACQUIRE:
			if (capture_feed(&sample)) {
				inference_result_t res;
				int ret = inference_run(
					(const float (*)[6])capture_window(),
					&res);

				if (ret == 0) {
					if (res.predicted_class == 0 &&
					    res.prob_A >=
						    CONFIDENCE_THRESHOLD) {
						printk(">>> GESTURE A detected (confidence %.2f) <<<\n",
						       (double)res.prob_A);
					} else {
						printk(">>> GESTURE IDLE detected (confidence %.2f) <<<\n",
						       (double)res.prob_IDLE);	
					}					

					printk("inference: %u cycles (%u us) [%s]\n",
					       (unsigned)inference_last_cycles(),
					       (unsigned)(inference_last_cycles() / 96U),
					       KERNEL_TAG);
				} else {
					printk("inference failed: %d\n", ret);
				}

				gpio_pin_set_dt(&led, 0);

				if (csv_logging) {
					capture_dump_csv();
				}
				/* Drop ticks missed during the blocking dump */
				k_sem_reset(&sample_sem);
				cooldown_end = k_uptime_get() + COOLDOWN_MS;
				state = STATE_COOLDOWN;
			}
			break;

		case STATE_COOLDOWN:
			if (k_uptime_get() >= cooldown_end) {
				/* Purge button presses swallowed during capture and cooldown */
				k_sem_reset(&button_sem); 
				state = STATE_IDLE;
			}
			break;
		}
	}

	return 0;
}
