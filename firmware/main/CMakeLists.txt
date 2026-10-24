idf_component_register(
    SRCS "app_main.cpp" "commissioning.cpp" "local_display.cpp" "sensors.cpp"
    INCLUDE_DIRS "."
    REQUIRES control_core driver esp_timer nvs_flash esp_system esp_lcd
             espressif__ds18b20 espressif__onewire_bus
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
