# /main/CMakeLists.txt
idf_component_register(
    SRCS "main.c" "beeper.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        lvgl__lvgl
        esp_lcd_st7796
)
