idf_component_register(
  SRCS "app_main.cpp" "network.cpp" "start_stop_button.cpp" "sensors.cpp" "web.cpp" "web_state.cpp" "bounded_receive.cpp"
       "panel.cpp" "panel_ui.cpp" "panel_canvas.cpp" "panel_render.cpp" "web_protocol.cpp" "ota_verify.cpp"
  INCLUDE_DIRS "."
  REQUIRES control_core driver esp_event esp_http_server esp_netif esp_partition esp_timer
           lwip esp_wifi app_update nvs_flash json bootloader_support esp_hw_support esp_lcd mbedtls
  EMBED_TXTFILES "ota_public_key.pem"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
