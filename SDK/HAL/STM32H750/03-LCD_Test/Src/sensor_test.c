/*
 * STM32H750 LCD diagnostic: MPU6050 + SHT30 on PB10(SCL)/PB11(SDA).
 *
 * This build intentionally uses slow, GPIO open-drain I2C rather than I2C2
 * hardware: the original 03-LCD_Test.ioc does not configure I2C2. This
 * keeps the test independent of a new CubeMX regeneration or I2C timing.
 * Both lines need pull-up resistors to the sensors' I/O voltage (3.3V).
 */
#include "sensor_test.h"
#include "lcd.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define I2C_PORT GPIOB
#define SCL_PIN  GPIO_PIN_10
#define SDA_PIN  GPIO_PIN_11

static void delay_half_bit(void)
{
    /* Intentionally slow and independent of DWT/debug hardware.
       Exact rate depends on SYSCLK/optimization, comfortably below 100 kHz. */
    for (volatile unsigned i = 0; i < 450U; ++i) {
        __NOP();
    }
}

static void scl(bool high)
{
    HAL_GPIO_WritePin(I2C_PORT, SCL_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void sda(bool high)
{
    HAL_GPIO_WritePin(I2C_PORT, SDA_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static bool scl_is_high(void)
{
    for (unsigned n = 0; n < 2000U; ++n) {
        if (HAL_GPIO_ReadPin(I2C_PORT, SCL_PIN) == GPIO_PIN_SET) return true;
    }
    return false;
}

static bool i2c_start(void)
{
    sda(true);
    delay_half_bit();
    scl(true);
    if (!scl_is_high()) return false;
    delay_half_bit();
    if (HAL_GPIO_ReadPin(I2C_PORT, SDA_PIN) != GPIO_PIN_SET) return false;
    sda(false);
    delay_half_bit();
    scl(false);
    delay_half_bit();
    return true;
}

static void i2c_stop(void)
{
    sda(false);
    delay_half_bit();
    scl(true);
    (void)scl_is_high();
    delay_half_bit();
    sda(true);
    delay_half_bit();
}

static bool i2c_write_byte(uint8_t value)
{
    for (unsigned bit = 0; bit < 8U; ++bit) {
        sda((value & 0x80U) != 0);
        delay_half_bit();
        scl(true);
        if (!scl_is_high()) return false;
        delay_half_bit();
        scl(false);
        value <<= 1;
    }
    sda(true);                    /* Release SDA during ACK */
    delay_half_bit();
    scl(true);
    if (!scl_is_high()) return false;
    delay_half_bit();
    const bool ack = HAL_GPIO_ReadPin(I2C_PORT, SDA_PIN) == GPIO_PIN_RESET;
    scl(false);
    delay_half_bit();
    return ack;
}

static uint8_t i2c_read_byte(bool ack)
{
    uint8_t data = 0;
    sda(true);                    /* Slave drives data */
    for (unsigned bit = 0; bit < 8U; ++bit) {
        data <<= 1;
        delay_half_bit();
        scl(true);
        (void)scl_is_high();
        delay_half_bit();
        if (HAL_GPIO_ReadPin(I2C_PORT, SDA_PIN) == GPIO_PIN_SET) data |= 1U;
        scl(false);
    }
    sda(!ack);                    /* ACK=low, NACK=high */
    delay_half_bit();
    scl(true);
    (void)scl_is_high();
    delay_half_bit();
    scl(false);
    sda(true);
    delay_half_bit();
    return data;
}

static bool i2c_probe(uint8_t address)
{
    bool ok = false;
    if (i2c_start()) ok = i2c_write_byte((uint8_t)(address << 1));
    i2c_stop();
    return ok;
}

static bool i2c_write_reg(uint8_t address, uint8_t reg, uint8_t val)
{
    bool ok = false;
    if (!i2c_start()) goto done;
    if (!i2c_write_byte((uint8_t)(address << 1))) goto done;
    if (!i2c_write_byte(reg)) goto done;
    ok = i2c_write_byte(val);
done:
    i2c_stop();
    return ok;
}

static bool i2c_read_reg(uint8_t address, uint8_t reg, uint8_t *data, unsigned count)
{
    bool ok = false;
    if (!i2c_start()) goto done;
    if (!i2c_write_byte((uint8_t)(address << 1))) goto done;
    if (!i2c_write_byte(reg)) goto done;
    if (!i2c_start()) goto done;   /* Repeated START */
    if (!i2c_write_byte((uint8_t)((address << 1) | 1U))) goto done;
    for (unsigned i = 0; i < count; ++i) {
        data[i] = i2c_read_byte(i + 1U < count);
    }
    ok = true;
done:
    i2c_stop();
    return ok;
}

static uint8_t sht_crc(const uint8_t *data, unsigned count)
{
    uint8_t crc = 0xFFU;
    for (unsigned i = 0; i < count; ++i) {
        crc ^= data[i];
        for (unsigned b = 0; b < 8U; ++b) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static bool sht_read(uint8_t address, int32_t *temp_centi, int32_t *rh_tenths)
{
    bool ok = false;
    uint8_t bytes[6];
    /* Single-shot, high repeatability, clock stretching disabled (0x2400). */
    if (!i2c_start()) goto done;
    if (!i2c_write_byte((uint8_t)(address << 1))) goto done;
    if (!i2c_write_byte(0x24U)) goto done;
    if (!i2c_write_byte(0x00U)) goto done;
    i2c_stop();
    HAL_Delay(20);
    if (!i2c_start()) goto done;
    if (!i2c_write_byte((uint8_t)((address << 1) | 1U))) goto done;
    for (unsigned i = 0; i < 6U; ++i) {
        bytes[i] = i2c_read_byte(i != 5U);
    }
    if (sht_crc(bytes, 2U) != bytes[2] ||
        sht_crc(&bytes[3], 2U) != bytes[5]) goto done;
    {
        uint32_t rt = ((uint32_t)bytes[0] << 8) | bytes[1];
        uint32_t rh = ((uint32_t)bytes[3] << 8) | bytes[4];
        *temp_centi = -4500 + (int32_t)((17500U * rt + 32767U) / 65535U);
        *rh_tenths = (int32_t)((1000U * rh + 32767U) / 65535U);
    }
    ok = true;
done:
    i2c_stop();
    return ok;
}

static void line(uint16_t y, const char *text)
{
    char padded[21];
    /* The 0.96-inch panel is 160 px wide: 20 glyphs at 8 px each. */
    (void)snprintf(padded, sizeof padded, "%-20.20s", text);
    LCD_ShowString(0, y, 160, 16, 16, (uint8_t *)padded);
}

static void format_accel(char *out, size_t size, char axis, int16_t raw)
{
    /* MPU6050 power-on accelerometer range is +/-2g, 16384 counts/g. */
    const int32_t centi = ((int32_t)raw * 100) / 16384;
    const int32_t absolute = centi < 0 ? -centi : centi;
    (void)snprintf(out, size, "%c%c%ld.%02ld", axis,
                   centi < 0 ? '-' : '+',
                   (long)(absolute / 100), (long)(absolute % 100));
}

void SensorTest_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(I2C_PORT, SCL_PIN | SDA_PIN, GPIO_PIN_SET);
    GPIO_InitTypeDef pins = {0};
    pins.Pin = SCL_PIN | SDA_PIN;
    pins.Mode = GPIO_MODE_OUTPUT_OD;
    pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(I2C_PORT, &pins);
    sda(true);
    scl(true);
    line(0, "H750 SENSOR TEST");
    line(16, "MPU6050: scanning");
    line(32, "X:-- Y:-- Z:--");
    line(48, "SHT30: scanning");
    line(64, "T:--.-C H:--.-%");
}

void SensorTest_Update(void)
{
    char msg[64];
    uint8_t mpu = 0;
    uint8_t sht = 0;
    uint8_t who = 0;
    uint8_t axes[6];

    if (i2c_probe(0x68U)) mpu = 0x68U;
    else if (i2c_probe(0x69U)) mpu = 0x69U;

    if (mpu == 0) {
        line(16, "MPU6050: NOT FOUND");
        line(32, "X:-- Y:-- Z:--");
    } else if (!i2c_read_reg(mpu, 0x75U, &who, 1U)) {
        line(16, "MPU6050: ID READ ERR");
        line(32, "X:-- Y:-- Z:--");
    } else if (who != 0x68U) {
        (void)snprintf(msg, sizeof msg, "MPU@%02X BAD ID:%02X", mpu, who);
        line(16, msg);
        line(32, "X:-- Y:-- Z:--");
    } else {
        (void)snprintf(msg, sizeof msg, "MPU:OK 0x%02X ID:%02X", mpu, who);
        line(16, msg);
        if (!i2c_write_reg(mpu, 0x6BU, 0x00U)) {
            line(32, "MPU WAKE ERROR");
        } else {
            HAL_Delay(3);
            if (i2c_read_reg(mpu, 0x3BU, axes, 6U)) {
                const int16_t x = (int16_t)(((uint16_t)axes[0] << 8) | axes[1]);
                const int16_t y = (int16_t)(((uint16_t)axes[2] << 8) | axes[3]);
                const int16_t z = (int16_t)(((uint16_t)axes[4] << 8) | axes[5]);
                char sx[16], sy[16], sz[16];
                format_accel(sx, sizeof sx, 'X', x);
                format_accel(sy, sizeof sy, 'Y', y);
                format_accel(sz, sizeof sz, 'Z', z);
                (void)snprintf(msg, sizeof msg, "%s %s %s", sx, sy, sz);
                line(32, msg);
            } else {
                line(32, "MPU AXIS READ ERR");
            }
        }
    }

    if (i2c_probe(0x44U)) sht = 0x44U;
    else if (i2c_probe(0x45U)) sht = 0x45U;

    if (sht == 0) {
        line(48, "SHT30: NOT FOUND");
        line(64, "T:--.-C H:--.-%");
    } else {
        int32_t temp = 0, rh = 0;
        (void)snprintf(msg, sizeof msg, "SHT30:OK 0x%02X", sht);
        line(48, msg);
        if (!sht_read(sht, &temp, &rh)) {
            line(64, "SHT CRC/READ ERROR");
        } else {
            int32_t abs_temp = temp < 0 ? -temp : temp;
            (void)snprintf(msg, sizeof msg, "T:%s%ld.%02ldC H:%ld.%01ld%%",
                           temp < 0 ? "-" : "", (long)(abs_temp / 100),
                           (long)(abs_temp % 100), (long)(rh / 10), (long)(rh % 10));
            line(64, msg);
        }
    }
}
