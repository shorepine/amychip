# AMY on a chip 

This repository collects "AMYchip" implementations, where we run the [AMY](https://github.com/shorepine/AMY) synthesizer on a full chip (doing nothing else), servicing requests over i2c (and future, SPI etc).

We have
 - [`esp32s3`](https://github.com/shorepine/amychip/tree/main/esp32s3)

And soon want to have
 - `rp2350`
 - `esp32p4`
 - `daisy`


## How to

Clone AMY into a folder next to this one. For now, use the `amychip-combined` branch of AMY

```bash
git clone https://github.com/shorepine/amy
cd amy; git checkout amychip-combined; cd ..
git clone https://github.com/shorepine/amychip
```


Set your I2S and I2C pins at the top of the `amychip.c` in your chip's folder:

```c
#define I2S_BCLK 13 
#define I2S_LRCLK 12
#define I2S_DIN 11 // data going to the codec, eg DAC data
#define I2C_SLAVE_SCL 5  
#define I2C_SLAVE_SDA 4 
#define I2C_MASTER_SCL 17
#define I2C_MASTER_SDA 18
#define I2S_DOUT 16 // data coming from the codec, eg ADC  data
```

Set up your DAC / ADC codec. I'm using the [`WM8960`](https://www.sparkfun.com/products/21250) codec. Wire it up like:

```
Tulip or etc I2C/Mabee/Grove SCL -> I2C_SLAVE_SCL
Tulip or etc I2C/Mabee/Grove SDA -> I2C_SLAVE_SDA
Tulip or etc I2C/Mabee/Grove GND -> MCU GND
Tulip or etc I2C/Mabee/Grove 3.3V -> MCU 3V3
USB 5V -> MCU (we need 5V to power this codec)
WM8960 GND -> Audio in GND
WM8960 LINPUT1 -> Audio in L
WM8960 RINPUT1 -> Audio in R
WM8960 HPL -> Audio out L
WM8960 OUT3 -> Audio out GND  *** NOTE: Audio-out "ground" is actually a special "mid-voltage" on OUT3
WM8960 HPR -> Audio out R
WM8960 BLCK -> I2S_BCLK
WM8960 DACLRC -> I2S_LRCLK (shared)
WM8960 DACDAT -> I2S_DIN
WM8960 ADCLRC -> I2S_LRCLK (shared)
WM8960 ADCDAT -> I2S_DOUT
WM8960 SCL -> I2C_MASTER_SCL
WM8960 SDA -> I2C_MASTER_SDA
WM8960 3V3 -> MCU 3V3
WM8960 VIN -> MCU 5V 
WM8960 GND -> MCU GND 
```

Compile / flash `amychip` using the instructions in the chip's folder.

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
 - ~~`memorypcm` / sample loading~~
 - stderr feedback over I2C
 - `sequencer.c`, sending interrupts to the "main" i2c host
 - SPI? UART? 

 


