# AMY on a chip 

An ESP-IDF implementation of running [AMY](https://github.com/shorepine/AMY) on a full chip 

[See the main README for setup instructions.](https://github.com/shorepine/amychip/blob/main/README.md)

## ESP32-S3 setup

Install and export the [ESP-IDF](https://github.com/espressif/esp-idf). Use the `master` branch.

```bash
git clone https://github.com/espressif/esp-idf
cd esp-idf
./install.sh esp32s3
source ./export.sh
cd ..
```

Compile and flash your AMY board

```bash
cd amychip
idf.py set-target esp32s3
idf.py flash
```
