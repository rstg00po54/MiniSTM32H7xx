# STM32H750 internal-Flash LCD sensor test (GCC/CMake)

This target is for the **STM32H750VBT6**, with **128 KiB internal flash** at
`0x08000000`, and the board's **160x80 ST7735 LCD**.

The CMake target compiles the LCD BSP, but intentionally omits the full-screen
WeAct logo bitmap arrays, which do not fit in the H750's small internal Flash.
The previous LCD inversion fix in `st7735.c` is retained.

## Wiring

From the supplied board schematic:

- PB10: sensor I2C SCL
- PB11: sensor I2C SDA
- Both pins are used as **open-drain GPIO software I2C**, so the test does not
  depend on enabling the hardware I2C2 peripheral in the existing .ioc file.
- Use appropriate 3.3 V pull-up resistors on the bus.
- MPU6050 responds at 0x68 or 0x69. It is accepted only if WHO_AM_I == 0x68.
- SHT30 responds at 0x44 or 0x45. Measurements are CRC-8 validated.

Screen lines: header, MPU ID/status, XYZ acceleration in g, SHT status,
and temperature/relative humidity. A missing device reports NOT FOUND.

## Build on Ubuntu

```bash
sudo apt install -y cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi openocd
cd SDK/HAL/STM32H750/03-LCD_Test
cmake --preset Release
cmake --build --preset Release --parallel "$(nproc)"
arm-none-eabi-size build/Release/03-LCD_Test.elf
arm-none-eabi-objcopy -O ihex build/Release/03-LCD_Test.elf build/Release/03-LCD_Test.hex
```

If the GNU linker reports `region FLASH overflowed`, inspect the reported size
rather than changing the 128K flash limit.

## Flash via DAPLink / CMSIS-DAP

Connect SWDIO-PA13, SWCLK-PA14, GND-GND, target voltage reference and,
optionally, NRST. With the target supplied at its required voltage:

```bash
sudo openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/stm32h7x.cfg -c "adapter speed 1000" -c "program build/Release/03-LCD_Test.hex verify reset exit"
```

## Notes

- This is a **diagnostic firmware**, not yet hardware validated. It requires
  the external MPU6050 and SHT30 to be correctly soldered and powered.
- The software I2C test is intentionally slow, and it can help distinguish
  communication failure from sensor-read failure without involving I2C2 timing
  registers.
- Do not enable I2C2 on PB10/PB11 simultaneously with the GPIO software I2C
  implementation. If you later migrate to hardware I2C2, replace this driver
  and coordinate the pin configuration in CubeMX.

## 12 MHz HSE and USART1 serial console

This board variant uses a **12 MHz external crystal** (HSE oscillator mode).
PLL1 is configured for a 240 MHz CPU clock (M=3, N=120, P=2), keeping
HCLK and PCLK2 at 120 MHz. Update `HSE_VALUE` and the CubeMX `.ioc`
together if changing the crystal.

The firmware uses a lightweight, polling USART1 driver (no HAL UART module
needed; it is absent from this compact example's bundled HAL sources).

- PA9: USART1 TX (connect to USB-TTL adapter RX)
- PA10: USART1 RX (connect to USB-TTL adapter TX; incoming bytes are echoed)
- GND: shared with USB-TTL adapter
- Terminal: **115200 baud, 8 data bits, no parity, 1 stop bit**, no flow control
- Voltage: **3.3 V TTL only**, never RS-232 +/- voltage; do not connect adapter
  5 V to the MCU pins

On reset you should see a startup banner and clock values, then repeating
`[UART] alive ...` lines and the MPU6050/SHT30 LCD status mirrored to serial.
After opening the terminal, press the board's RESET button to see the banner.

```bash
# Example, adjust the serial device name
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```

The 12 MHz setting applies to this example only; other 25 MHz reference-board
examples in the repository have not been changed. Physical hardware testing
and a complete ARM cross-compilation are still required.
