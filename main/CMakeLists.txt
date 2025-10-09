# /main/CMakeLists.txt
idf_component_register(
    SRCS 
        "main.c" 
        "touch_ft6336.c" 
        "beeper.c" 
        "touch_debug.c" 
        "led_addr.c" 
        "ui_theme.c" 
        "ui_actions.c"
        "fm24cl64.c"
        "fram_wl.c"
        "prefs_store.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver              # GPIO/I2C/SPI/LEDC
        esp_lcd             # panel + IO base
        lvgl__lvgl          # managed component name in build logs
        esp_lcd_st7796
        esp_lcd_touch_ft5x06
        fatfs               # <-- needed only if USE_SD_CARD=1
        vfs                 # <-- needed only if USE_SD_CARD=1
        sdmmc               # <-- needed only if USE_SD_CARD=1
)
