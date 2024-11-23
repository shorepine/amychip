#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_task.h"
#include "driver/i2c_master.h"

#include "amy.h"
#include "examples.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2s_std.h"

static const char *TAG = "amy-chip";

i2s_chan_handle_t tx_handle;
i2s_chan_handle_t rx_handle;


#define I2S_BCLK 13 
#define I2S_LRCLK 12
#define I2S_DIN 11 // data going to the codec, eg DAC data
#define I2C_SLAVE_SCL 5  
#define I2C_SLAVE_SDA 4 
#define I2C_MASTER_SCL 17
#define I2C_MASTER_SDA 18
#define I2S_DOUT 16 // data coming from the codec, eg ADC  data
#define I2S_SAMPLE_TYPE I2S_BITS_PER_SAMPLE_16BIT
typedef int16_t i2s_sample_type;


// mutex that locks writes to the delta queue
SemaphoreHandle_t xQueueSemaphore;

void delay_ms(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

// Task handles for the renderers, multicast listener and main
TaskHandle_t amy_render_handle;
TaskHandle_t alles_fill_buffer_handle;


#define ALLES_TASK_COREID (1)
#define ALLES_RENDER_TASK_COREID (0)
#define ALLES_FILL_BUFFER_TASK_COREID (1)
#define ALLES_RENDER_TASK_PRIORITY (ESP_TASK_PRIO_MAX-1 )
#define ALLES_FILL_BUFFER_TASK_PRIORITY (ESP_TASK_PRIO_MAX-1)
#define ALLES_TASK_NAME             "alles_task"
#define ALLES_RENDER_TASK_NAME      "alles_r_task"
#define ALLES_FILL_BUFFER_TASK_NAME "alles_fb_task"
#define ALLES_TASK_STACK_SIZE    (8 * 1024) 
#define ALLES_RENDER_TASK_STACK_SIZE (8 * 1024)
#define ALLES_FILL_BUFFER_TASK_STACK_SIZE (8 * 1024)


// i2c stuff
#define I2C_CLK_FREQ 400000
#include "esp32-hal-i2c-slave.h"
#define DATA_LENGTH 255
#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)
#define I2C_SLAVE_NUM I2C_NUMBER(1) /*!< I2C port number for slave dev */
#define I2C_SLAVE_TX_BUF_LEN (2 * DATA_LENGTH)              /*!< I2C slave tx buffer size */
#define I2C_SLAVE_RX_BUF_LEN (2 * DATA_LENGTH)              /*!< I2C slave rx buffer size */
#define ESP_SLAVE_ADDR 0x58             /*!< ESP32 slave address, you can set any 7bit value */
#define I2C_MASTER_NUM I2C_NUMBER(0) /*!< I2C port number for master dev */
#define I2C_MASTER_TX_BUF_DISABLE 0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0  

i2c_master_bus_handle_t tool_bus_handle;
#define I2C_TOOL_TIMEOUT_VALUE_MS (50)
esp_err_t i2c_master_write_pcm9211(uint8_t *data_wr, size_t size_wr) {

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = I2C_CLK_FREQ,
        .device_address = 0x40,
    };
    i2c_master_dev_handle_t dev_handle;
    if (i2c_master_bus_add_device(tool_bus_handle, &i2c_dev_conf, &dev_handle) != ESP_OK) {
        return 1;
    }
    esp_err_t ret = i2c_master_transmit(dev_handle, data_wr, size_wr, I2C_TOOL_TIMEOUT_VALUE_MS);
    if (ret == ESP_OK) {

    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Bus is busy");
    } else {
        ESP_LOGW(TAG, "Write Failed");
    }
    if (i2c_master_bus_rm_device(dev_handle) != ESP_OK) {
        return 1;
    }
    return 0;
}

void writeRegister(uint8_t reg, uint16_t value) {
  uint8_t data[2];
  data[0] = (reg ); 
  data[1] = (value); 
  if(i2c_master_write_pcm9211(data,2)) fprintf(stderr, "bad\n");
}


esp_err_t setup_pcm9211(void) {
    // do the register dance

    // #**************************************
    // #this script is for SPDIF-->RXIN0-->DIR-->MainOutput, Record sound from SPDIF to PC through TAS1020

    // #So
    // #1, Chose RXIN0 to DIR
    // #2, Active DIR
    // #3, chose DIR output as Mainoutput's source.

    // #Also HW modification
    // #1, Flying to High Level(3.3V) to make sure U7's output is Hi-Z
    // #or 2, TAS1020 output logic high on P1.2 I2S enable signal. 
    // #**************************************


    // #System RST Control
    // #w 80 40 00
    // w 80 40 33
    writeRegister(0x40, 0x33);  // Power down ADC, power down DIR, power down DIT, power down OSC
    // w 80 40 C0
    writeRegister(0x40, 0xC0);  // Normal operation for all

    // #XTI Source, Clock (SCK/BCK/LRCK) Frequency Setting
    // # XTI 24.5760 for SCLK 12.288 and BCK 3.072, LRCK 48k = XTI/512
    // w 80 31 1A
    writeRegister(0x31, 0x1a);
    // w 80 33 22
    writeRegister(0x33, 0x22);
    // w 80 20 00
    writeRegister(0x20, 0x00);
    // w 80 24 00
    writeRegister(0x24, 0x00);
    // #ADC clock source is chosen by REG42
    // w 80 26 81
    writeRegister(0x26, 0x81);

    // #XTI Source, Secondary Bit/LR Clock (SBCK/SLRCK) Frequency Setting
    // w 80 33 22
    writeRegister(0x33, 0x22);

    // #*********************************************************
    // #-------------------------------Start DIR settings---------------------------------------
    // #REG. 21h, DIR Receivable Incoming Biphase's Sampling Frequency Range Setting
    // w 80 21 00
    writeRegister(0x21, 0x10);   // 0x10 = normal, 28-108 kHz

    // #REG. 22h, DIR CLKSTP and VOUT delay
    // w 80 22 01
    writeRegister(0x22, 0x01);

    // #REG. 23h, DIR OCS start up wait time and Process for Parity Error Detection and ERROR Release Wait Time Setting
    // w 80 23 04
    writeRegister(0x23, 0x04);

    // # REG 27h DIR Acceptable fs Range Setting & Mask
    // w 80 27 00
    //writeRegister(0x27, 0xd7);  // 0xd7: Limit to 32, 44, 48 +/- 2%
    writeRegister(0x27, 0x00);  // 0xd7: Limit to 32, 44, 48 +/- 2%

    // # REG 2Fh, DIR Output Data Format, 24bit I2S mode
    // w 80 2F 04
    writeRegister(0x2f, 0x04);

    // # REG. 30h, DIR Recovered System Clock (SCK) Ratio Setting
    // w 80 30 02
    writeRegister(0x30, 0x02);

    // #REG. 32h, DIR Source, Secondary Bit/LR Clock (SBCK/SLRCK) Frequency Setting
    // w 80 32 22
    writeRegister(0x32, 0x22);

    // #REG 34h DIR Input Biphase Signal Source Select and RXIN01 Coaxial Amplifier
    // #--PWR down amplifier, Select RXIN2
    // #w 80 34 C2
    // #--PWR up amplifier, select RXIN0
    // w 80 34 00
    //writeRegister(0x34, 0x00);
    // #--PWR up amplifier, select RXIN1
    // #w 80 34 01
    writeRegister(0x34, 0x01);

    // #REG. 37h, Port Sampling Frequency Calculator Measurement Target Setting, Cal and DIR Fs
    // w 80 37 00
    writeRegister(0x37, 0x00);
    // #REG 38h rd DIR Fs
    // r 80 38 01
    writeRegister(0x38, 0x01);
    // #***********************************************************
    // #------------------------------------ End DIR settings------------------------------------------

    // #***********************************************************
    // #---------------------------------Start  MainOutput Settings--------------------------------------
    // #MainOutput
    // #REG. 6Ah, Main Output & AUXOUT Port Control
    // w 80 6A 00
    writeRegister(0x6a, 0x00);

    // #REG. 6Bh, Main Output Port (SCKO/BCK/LRCK/DOUT) Source Setting
    // w 80 6B 11
    //writeRegister(0x6b, 0x11);  // 0x11 = locked to DIR (0x00 for ADC/DIR auto)
    // dan had
    //writeRegister(0x6b, 0x00);  // 0x11 = locked to DIR (0x00 for ADC/DIR auto)
    writeRegister(0x6b, 0x14);
    //writeRegister(0x61, 0x14);
    //writeRegister(0x34, 0xCF);

    // #REG. 6Dh, MPIO_B & Main Output Port Hi-Z Control
    // w 80 6D 00
    writeRegister(0x6d, 0x00);
    // #***********************************************************
    // #------------------------------------ End MainOutput settings------------------------------------------
    return ESP_OK;
}

static esp_err_t i2c_master_init(void) {
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL,
        .sda_io_num = I2C_MASTER_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    if (i2c_new_master_bus(&i2c_bus_config, &tool_bus_handle) != ESP_OK) {
        return 1;
    }
    return ESP_OK;
}



static void i2c_slave_request_cb(uint8_t num, uint8_t *cmd, uint8_t cmd_len, void * arg) {
    if (cmd_len > 0) {
        // first write to master
        i2cSlaveWrite(I2C_SLAVE_NUM, cmd, cmd_len, 0);
    } else {
        // cmd_len == 0 means master want more data from slave
        // we just send one byte 0 each time to master here
        uint8_t extra_data = 0x00;
        i2cSlaveWrite(I2C_SLAVE_NUM, &extra_data, 1, 0);
    }
}

static void i2c_slave_receive_cb(uint8_t num, uint8_t * data, size_t len, bool stop, void * arg) {
    if (len > 0) {
        data[len]= 0;
        amy_play_message((char*)data);
    }
}

static esp_err_t i2c_slave_init(void) {
    i2cSlaveAttachCallbacks(I2C_SLAVE_NUM, i2c_slave_request_cb, i2c_slave_receive_cb, NULL);
    return i2cSlaveInit(I2C_SLAVE_NUM, I2C_SLAVE_SDA, I2C_SLAVE_SCL, ESP_SLAVE_ADDR, I2C_CLK_FREQ, I2C_SLAVE_RX_BUF_LEN, I2C_SLAVE_TX_BUF_LEN);
}



// AMY synth states
extern struct state amy_global;
extern uint32_t event_counter;
extern uint32_t message_counter;

void esp_show_debug(uint8_t t) {

}

// Render the second core
void esp_render_task( void * pvParameters) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        amy_render(0, AMY_OSCS/2, 1);
        xTaskNotifyGive(alles_fill_buffer_handle);
    }
}

extern int16_t amy_in_block[AMY_BLOCK_SIZE*AMY_NCHANS];

// Make AMY's FABT run forever , as a FreeRTOS task 
void esp_fill_audio_buffer_task() {
    size_t read = 0;
    size_t written = 0;
    while(1) {
        AMY_PROFILE_START(AMY_ESP_FILL_BUFFER)
        i2s_channel_read(rx_handle, amy_in_block, AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS, &read, portMAX_DELAY);

        // Get ready to render
        amy_prepare_buffer();
        // Tell the other core to start rendering
        xTaskNotifyGive(amy_render_handle);
        // Render me
        amy_render(AMY_OSCS/2, AMY_OSCS, 0);
        // Wait for the other core to finish
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Write to i2s
        int16_t *block = amy_fill_buffer();
        AMY_PROFILE_STOP(AMY_ESP_FILL_BUFFER)

        i2s_channel_write(tx_handle, block, AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS, &written, portMAX_DELAY);

        if(written != AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS || read != AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS) {
            fprintf(stderr,"i2s underrun: [w %d,r %d] vs %d\n", written, read, AMY_BLOCK_SIZE * AMY_BYTES_PER_SAMPLE * AMY_NCHANS);
        }

    }
}



// init AMY from the esp. wraps some amy funcs in a task to do multicore rendering on the ESP32 
amy_err_t esp_amy_init() {
    amy_start(2, 1, 1, 1);
    // We create a mutex for changing the event queue and pointers as two tasks do it at once
    xQueueSemaphore = xSemaphoreCreateMutex();

    // Create the second core rendering task
    xTaskCreatePinnedToCore(&esp_render_task, ALLES_RENDER_TASK_NAME, ALLES_RENDER_TASK_STACK_SIZE, NULL, ALLES_RENDER_TASK_PRIORITY, &amy_render_handle, ALLES_RENDER_TASK_COREID);

    // Wait for the render tasks to get going before starting the i2s task
    delay_ms(100);

    // And the fill audio buffer thread, combines, does volume & filters
    xTaskCreatePinnedToCore(&esp_fill_audio_buffer_task, ALLES_FILL_BUFFER_TASK_NAME, ALLES_FILL_BUFFER_TASK_STACK_SIZE, NULL, ALLES_FILL_BUFFER_TASK_PRIORITY, &alles_fill_buffer_handle, ALLES_FILL_BUFFER_TASK_COREID);

    return AMY_OK;
}



// Setup I2S
amy_err_t setup_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_SLAVE);
    i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AMY_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK,
            .ws = I2S_LRCLK,
            .dout = I2S_DIN,
            .din = I2S_DOUT,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* Initialize the channel */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);
    i2s_channel_init_std_mode(rx_handle, &std_cfg);

    /* Before writing data, start the TX channel first */
    i2s_channel_enable(tx_handle);
    i2s_channel_enable(rx_handle);
    return AMY_OK;
}

void app_main(void)
{
    printf("Welcome to the AMY chip implementation!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    check_init(&i2c_master_init, "i2c_master");
    check_init(&i2c_slave_init, "i2c_slave");
    check_init(&setup_pcm9211, "pcm9211");
    check_init(&setup_i2s, "i2s");
    esp_amy_init();
    amy_reset_oscs();

    struct event e = amy_default_event();
    e.time = amy_sysclock();
    e.freq_coefs[0] = 440;
    e.wave = SINE;
    e.velocity = 1;
    amy_add_event(e);

    while(1) {
        delay_ms(10);
    }
}
