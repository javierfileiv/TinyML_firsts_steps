#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <errno.h>

#include "imu.h"

/* MPU6050 registers not exposed by the Zephyr driver */
#define MPU6050_REG_SMPLRT_DIV 0x19
#define MPU6050_REG_CONFIG     0x1A

/* DLPF_CFG = 3: ~44 Hz bandwidth, gyro output rate drops to 1 kHz */
#define MPU6050_DLPF_CFG 3

/* Sample rate = 1 kHz / (1 + 9) = exactly 100 Hz */
#define MPU6050_SMPLRT_DIV 9

static const struct device *const mpu6050 = DEVICE_DT_GET(DT_NODELABEL(mpu6050));
static const struct i2c_dt_spec mpu6050_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(mpu6050));

static imu_ready_cb_t app_cb;

static struct sensor_trigger data_ready_trigger = {
	.type = SENSOR_TRIG_DATA_READY,
	.chan = SENSOR_CHAN_ALL,
};

static void data_ready_trampoline(const struct device *dev,
				  const struct sensor_trigger *trig)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(trig);

	if (app_cb != NULL) {
		app_cb();
	}
}

int imu_init(void)
{
	int ret;

	if (!device_is_ready(mpu6050)) {
		return -ENODEV;
	}

	/* The driver leaves SMPLRT_DIV and DLPF at reset values, which would
	 * pulse INT at 8 kHz unfiltered. Program the 100 Hz cadence here
	 */
	ret = i2c_reg_write_byte_dt(&mpu6050_i2c, MPU6050_REG_CONFIG,
				    MPU6050_DLPF_CFG);
	if (ret < 0) {
		return ret;
	}

	return i2c_reg_write_byte_dt(&mpu6050_i2c, MPU6050_REG_SMPLRT_DIV,
				     MPU6050_SMPLRT_DIV);
}

int imu_read(struct imu_sample *sample)
{
	struct sensor_value accel[3];
	struct sensor_value gyro[3];
	int ret;

	ret = sensor_sample_fetch(mpu6050);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(mpu6050, SENSOR_CHAN_ACCEL_XYZ, accel);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(mpu6050, SENSOR_CHAN_GYRO_XYZ, gyro);
	if (ret < 0) {
		return ret;
	}

	sample->ax = sensor_value_to_float(&accel[0]);
	sample->ay = sensor_value_to_float(&accel[1]);
	sample->az = sensor_value_to_float(&accel[2]);
	sample->gx = sensor_value_to_float(&gyro[0]);
	sample->gy = sensor_value_to_float(&gyro[1]);
	sample->gz = sensor_value_to_float(&gyro[2]);

	return 0;
}

int imu_start(imu_ready_cb_t cb)
{
	app_cb = cb;

	return sensor_trigger_set(mpu6050, &data_ready_trigger,
				  data_ready_trampoline);
}
