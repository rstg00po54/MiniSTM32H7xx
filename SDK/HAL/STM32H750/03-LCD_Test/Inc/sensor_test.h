#ifndef SENSOR_TEST_H
#define SENSOR_TEST_H

/* Diagnostic firmware for the board sensors on PB10/PB11.
 * GPIO-based I2C is deliberate: the original CubeMX .ioc does not enable I2C2.
 */
void SensorTest_Init(void);
void SensorTest_Update(void);

#endif /* SENSOR_TEST_H */
