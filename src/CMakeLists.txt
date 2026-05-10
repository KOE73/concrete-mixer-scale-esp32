# Display widgets and sinks are small and change together during UI diagnostics.
# Keep this directory globbed so adding a display-only .cpp does not require
# a CMake edit every time.
file(GLOB DISPLAY_SRCS "display/*.cpp")

idf_component_register(
    SRCS
        "main.cpp"
        ${DISPLAY_SRCS}
        "measurement/load_cell_reader.cpp"
        "measurement/load_cell_sampler.cpp"
        "platform/hx711_bus.cpp"
        "platform/hx711_reader.cpp"
        "processing/weight_filters.cpp"
        "processing/weight_processor.cpp"
        "settings/settings_store.cpp"
        "storage/web_assets.cpp"
        "web/web_server.cpp"
        "web/wifi_manager.cpp"
    INCLUDE_DIRS
        "../include"
    REQUIRES
        driver
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        freertos
        json
        nvs_flash
        spiffs
)

spiffs_create_partition_image(www ../www FLASH_IN_PROJECT)
