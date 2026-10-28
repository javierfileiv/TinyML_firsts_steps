#ifndef EDGE_AI_IMU_H
#define EDGE_AI_IMU_H

/* One 6-axis IMU sample in physical units
 * Acceleration in m/s^2, angular rate in rad/s
 */
struct imu_sample {
	float ax;
	float ay;
	float az;
	float gx;
	float gy;
	float gz;
};

/* Checks that the MPU6050 device is ready and programs the sample rate
 * to exactly 100 Hz (DLPF enabled, 1 kHz gyro output divided by 10)
 * Returns 0 on success, negative errno otherwise
 */
int imu_init(void);

/* Fetches one sample from the sensor and converts it to physical units
 * Returns 0 on success, negative errno otherwise
 */
int imu_read(struct imu_sample *sample);

/* Application callback invoked on each data-ready interrupt
 * Called from the system workqueue, must stay short (give a semaphore)
 */
typedef void (*imu_ready_cb_t)(void);

/* Registers the data-ready callback and starts interrupt-driven sampling
 * Returns 0 on success, negative errno otherwise
 */
int imu_start(imu_ready_cb_t cb);

#endif /* EDGE_AI_IMU_H */
