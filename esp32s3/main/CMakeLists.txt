idf_component_register(SRCS "amychip.c"
                    esp32-hal-i2c-slave.c
                    wm8960.c
                    ../../../amy/src/log2_exp2.c
                    ../../../amy/src/amy.c
                    ../../../amy/src/custom.c
                    ../../../amy/src/delay.c
                    ../../../amy/src/patches.c
                    ../../../amy/src/algorithms.c
                    ../../../amy/src/oscillators.c
                    ../../../amy/src/pcm.c
                    ../../../amy/src/filters.c
                    ../../../amy/src/envelope.c
                    ../../../amy/src/partials.c
                    ../../../amy/src/examples.c
                    ../../../amy/src/delay.c
                    ../../../amy/src/transfer.c

                    PRIV_REQUIRES spi_flash esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_ringbuf esp_timer driver
                    INCLUDE_DIRS "../../../amy/src")



target_compile_options(${COMPONENT_TARGET} PUBLIC 
    -Wno-uninitialized
    -DESP_PLATFORM
)

set_source_files_properties(../../../amy/src/amy.c
    PROPERTIES COMPILE_FLAGS
    -Wno-strict-aliasing
)
