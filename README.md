# ESP32 (+S3, P4) AMY dual core example

An ESP-IDF implementation of running [AMY](https://github.com/shorepine/AMY) in dual cores.

## How to use example

Clone AMY into a folder next to this one

```bash
git clone https://github.com/shorepine/amy
git clone https://github.com/shorepine/amy_dual_core_esp32
```

Set your I2S pins at the top of `hello_world_main.c` in this repository:

```c
#define CONFIG_I2S_BCLK 25
#define CONFIG_I2S_LRCLK 36
#define CONFIG_I2S_DIN 32
```

Install and export the [ESP-IDF](https://github.com/espressif/esp-idf). For the P4, use the `master` branch.

```bash
git clone https://github.com/espressif/esp-idf
cd esp-idf
./install.sh esp32p4
source ./export.sh
cd ..
```

Compile and flash your board, setting the `IDF_TARGET` to whatever you're using 
```bash
cd amy_dual_core_esp32
idf.py set-target esp32p4
idf.py flash
```

You should hear up to 20 polyphonic Juno-6 voices play out your I2S speaker. This uses both cores.




