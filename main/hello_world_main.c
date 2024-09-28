/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_task.h"
#include "driver/i2c_slave.h"


#include "amy.h"
#include "examples.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2s_std.h"

static const char *TAG = "amy-chip";

i2s_chan_handle_t tx_handle;
#define CONFIG_I2S_BCLK 13 // 25
#define CONFIG_I2S_LRCLK 12
#define CONFIG_I2S_DIN 11

// This can be 32 bit, int32_t -- helpful for digital output to a i2s->USB teensy3 board
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
#define I2C_SLAVE_NUM I2C_NUMBER(0) /*!< I2C port number for slave dev */
#define I2C_SLAVE_SCL_IO 5               /*!< gpio number for i2c slave clock */
#define I2C_SLAVE_SDA_IO 4               /*!< gpio number for i2c slave data */
#define I2C_SLAVE_TX_BUF_LEN (2 * DATA_LENGTH)              /*!< I2C slave tx buffer size */
#define I2C_SLAVE_RX_BUF_LEN (2 * DATA_LENGTH)              /*!< I2C slave rx buffer size */
#define ESP_SLAVE_ADDR 0x58             /*!< ESP32 slave address, you can set any 7bit value */


static void i2c_slave_request_cb(uint8_t num, uint8_t *cmd, uint8_t cmd_len, void * arg)
{
    //ESP_LOGI(TAG, "rcv_len: %d", cmd_len);
    // please not block or print in this callback function
    // because the SCL Stretching will be released if time is too long
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

static void i2c_slave_receive_cb(uint8_t num, uint8_t * data, size_t len, bool stop, void * arg)
{
    //ESP_LOGI(TAG, "rcv_len: %d", len);
    if (len > 0) {
        data[len]= 0;
        amy_play_message((char*)data);
        //ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);
    }
}

/**
 * @brief i2c slave initialization
 */
static esp_err_t i2c_slave_init(void)
{
    i2cSlaveAttachCallbacks(I2C_SLAVE_NUM, i2c_slave_request_cb, i2c_slave_receive_cb, NULL);
    return i2cSlaveInit(I2C_SLAVE_NUM, I2C_SLAVE_SDA_IO, I2C_SLAVE_SCL_IO, ESP_SLAVE_ADDR, I2C_CLK_FREQ, I2C_SLAVE_RX_BUF_LEN, I2C_SLAVE_TX_BUF_LEN);
}


/*
static IRAM_ATTR bool i2c_slave_rx_done_callback(i2c_slave_dev_handle_t channel, const i2c_slave_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

i2c_slave_dev_handle_t slave_handle;
QueueHandle_t s_receive_queue;
// on tulip, TS is 0x14, encoder is 0x41, dac is 0x59
amy_err_t setup_i2c() {
    i2c_slave_config_t i2c_slv_config = {
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .send_buf_depth = 256,
        .scl_io_num = 18,
        .sda_io_num = 17,
        .slave_addr = 0x58,
    };

    ESP_ERROR_CHECK(i2c_new_slave_device(&i2c_slv_config, &slave_handle));

    s_receive_queue = xQueueCreate(1, sizeof(i2c_slave_rx_done_event_data_t));
    i2c_slave_event_callbacks_t cbs = {
        .on_recv_done = i2c_slave_rx_done_callback,
    };
    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(slave_handle, &cbs, s_receive_queue));
    return AMY_OK;
}

*/


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

// Make AMY's FABT run forever , as a FreeRTOS task 
void esp_fill_audio_buffer_task() {
    while(1) {
        AMY_PROFILE_START(AMY_ESP_FILL_BUFFER)

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

        // We turn off writing to i2s on r10 when doing on chip debugging because of pins
        size_t written = 0;
        i2s_channel_write(tx_handle, block, AMY_BLOCK_SIZE * BYTES_PER_SAMPLE * AMY_NCHANS, &written, portMAX_DELAY);
        if(written != AMY_BLOCK_SIZE * BYTES_PER_SAMPLE * AMY_NCHANS) {
            fprintf(stderr,"i2s underrun: %d vs %d\n", written, AMY_BLOCK_SIZE * BYTES_PER_SAMPLE * AMY_NCHANS);
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
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AMY_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_I2S_BCLK,
            .ws = CONFIG_I2S_LRCLK,
            .dout = CONFIG_I2S_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* Initialize the channel */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);

    /* Before writing data, start the TX channel first */
    i2s_channel_enable(tx_handle);
    return AMY_OK;
}

void polyphony(uint32_t start, uint16_t patch) {
    struct event e = amy_default_event();
    e.time = start;
    e.load_patch = patch;
    strcpy(e.voices, "0,1,2,3,4,5,6,7,8,9,10");
    amy_add_event(e);
    start += 250;
    uint8_t note = 40;
    for(uint8_t i=0;i<11;i++) {
        e = amy_default_event();
        e.time = start;
        e.velocity=0.5;
        sprintf(e.voices, "%d", i);
        e.midi_note = note;
        amy_add_event(e);
        start += 1000;
        note += 2;
    }
}   


void app_main(void)
{
    printf("Hello world!\n");

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

    check_init(&i2c_slave_init, "i2c");
    check_init(&setup_i2s, "i2s");

    esp_amy_init();

    //int64_t start_time = amy_sysclock();
    amy_reset_oscs();

    //polyphony(start_time+500, 0);

    /*
    i2c_slave_rx_done_event_data_t rx_data;
    uint8_t *data_rd = (uint8_t *) malloc(255);
    data_rd[0] = 0;
    */
    while(1) {
        /*
        ESP_ERROR_CHECK(i2c_slave_receive(slave_handle, data_rd, 255));
        xQueueReceive(s_receive_queue, &rx_data, pdMS_TO_TICKS(10000));
        fprintf(stderr, "i2c message is %s\n", data_rd);
        */
        // Receive done.
        delay_ms(10);
    }
}
