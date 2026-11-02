idf_component_register(
  SRCS "app_main.cpp" "sensors.cpp" "web.cpp"
  INCLUDE_DIRS "."
  REQUIRES control_core driver esp_event esp_http_server esp_netif esp_partition esp_wifi app_update nvs_flash json
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
