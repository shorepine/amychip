# AMY on a chip 

An ESP-IDF implementation of running [AMY](https://github.com/shorepine/AMY) on a full chip (doing nothing else), servicing requests over i2c (and future, SPI etc)

## How to

Clone AMY into a folder next to this one

```bash
git clone https://github.com/shorepine/amy
git clone https://github.com/shorepine/amychip
```

Set your I2S and I2C pins at the top of `amychip.c` in this repository:

```c
#define CONFIG_I2S_BCLK 13 
#define CONFIG_I2S_LRCLK 12
#define CONFIG_I2S_DIN 11 // data going to the codec, eg DAC data
#define I2C_SLAVE_SCL_IO 5  
#define I2C_SLAVE_SDA_IO 4 
#define I2C_MASTER_SCL_IO 17
#define I2C_MASTER_SDA_IO 18
#define CONFIG_I2S_DOUT 16 // data coming from the codec, eg ADC  data
```

I'm using the [`WM8960`](https://www.sparkfun.com/products/21250) codec and a [ESP32-S3-N32R8 WROOM-2 DevKitC](https://www.adafruit.com/product/5364) for now. I wired everything up like:

```
Tulip or etc I2C/Mabee/Grove SCL -> ESP32S3 5
Tulip or etc I2C/Mabee/Grove SDA -> ESP32S3 4
Tulip or etc I2C/Mabee/Grove GND -> ESP32S3 GND
Tulip or etc I2C/Mabee/Grove 3.3V -> ESP32S3 3V3
USB 5V -> ESP32S3 UART (we need 5V to power this codec)
WM8960 GND -> Audio in GND
WM8960 LINPUT1 -> Audio in L
WM8960 RINPUT1 -> Audio in R
WM8960 HPL -> Audio out L
WM8960 OUT3 -> Audio out GND
WM8960 HPR -> Audio out R
WM8960 BLCK -> ESP32S3 13
WM8960 DACLRC -> ESP32S3 12 (shared)
WM8960 DACDAT -> ESP32S3 11
WM8960 ADCLRC -> ESP32S3 12 (shared)
WM8960 ADCDAT -> ESP32S3 16
WM8960 SCL -> ESP32S3 17
WM8960 SDA -> ESP32S3 18
WM8960 3V3 -> ESP32S3 3V3
WM8960 VIN -> ESP32S3 5V 
WM8960 GND -> ESP32S3 GND 
```


Install and export the [ESP-IDF](https://github.com/espressif/esp-idf). Use the `master` branch.

```bash
git clone https://github.com/espressif/esp-idf
cd esp-idf
./install.sh esp32s3
source ./export.sh
cd ..
```

Compile and flash your AMY board, setting the `IDF_TARGET` to whatever you're using 
```bash
cd amychip
idf.py set-target esp32s3
idf.py flash
```

Control the chip over I2C. In e.g. Tulip, that's as simple as 

```python
from machine import I2C
import amy
i2c = I2C(0, freq=400000)

amy.override_send= lambda x : i2c.writeto(0x58, bytes(x.encode('ascii')))
```

You can get this on Tulip World:

```python
world.download('amychip.py')
execfile('amychip.py')
```


## Protocol

For now, over i2c, we just send AMY messages encoded as ASCII to `0x58`. Nothing gets returned. 

TODO:
 - `memorypcm` / sample loading
 - stderr feedback over I2C
 - `sequencer.c`, sending interrupts to the "main" i2c host
 - SPI? UART? 

 


