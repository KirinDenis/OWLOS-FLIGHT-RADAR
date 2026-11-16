# chipguyhere's JC8012P4A1C display and board libraries (chipguy_JC8012P4A1C_display 1.0.0 and
# chipguy_JC8012P4A1C_board), vendored, with the Arduino calls they make supplied by shim/ (the same
# shim as components/chipguy_lcd7_7b, plus what these two call). fork addition. compiled only for the
# Guition JC8012P4A1C (core/config.h 17): for every other board the component is there but empty.
# changed from the library (marked "fork addition"): esp_lcd_panel_dpi_bb.c lets a raster band's
# chunk be read straight from the framebuffer (chipguy_lcd_dpi_panel_raster_direct), and the display
# class passes it on. the GSL3680 touch driver is the library's MIT one: it loads the controller's
# firmware (Silead's, GPL-2.0, so not in this tree) from NVS, where chipguyhere's
# ESP32-P4-C6-firmware-installer puts it once per board
set(SRCS "")
set(INCS "")
set(REQ "")
if(RADAR_DISPLAY EQUAL 17)
    set(SRCS "JC8012P4A1C_Board.cpp" "chipguy_ThreadsafeWire.cpp" "chipguy_JC8012P4A1C_display.cpp"
             "esp_lcd_panel_dpi_bb.c" "esp_lcd_jd9365.c" "gsl3680_touch.cpp"
             "shim/arduino_shim.cpp")
    set(INCS "." "shim")
endif()
if(IDF_TARGET STREQUAL "esp32p4")
    set(REQ esp_driver_ppa)
endif()
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS ${INCS}
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_timer esp_mm
                                esp_hw_support esp_rom freertos log heap nvs_flash mbedtls ${REQ})
if(SRCS)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC ARDUINO_ESP32P4_DEV=1 BOARD_HAS_PSRAM=1)   # the display header asks before its Arduino.h
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -Wno-unused-function
                           -Wno-missing-field-initializers -Wno-format -Wno-unused-but-set-variable)
endif()
