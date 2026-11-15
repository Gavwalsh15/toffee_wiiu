idf_component_register(
    SRCS "../src/main.cpp" "../src/app/application.cpp"
        "../src/gamepad/input_forwarder.cpp" "../src/audio/audio_stream.cpp"
        "../src/network/connection.cpp" "../src/network/wifi_setup.cpp"
        "../src/network/pairing_ap.cpp" "../src/gamepad/gamepad_runtime.cpp"
        "../src/transport/usb_stream.cpp" "../src/transport/bridge_link.cpp" "../src/video/video_stream.cpp"
        "../src/transport/cpu_profile.cpp"
        "../src/diagnostics/memory_diagnostics.cpp"
    INCLUDE_DIRS "../src" "../../src"
    REQUIRES esp_wifi esp_event esp_netif esp_timer esp_rom nvs_flash
        esp_driver_gpio esp_driver_usb_serial_jtag wpa_supplicant lwip
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_add_binary_data(${COMPONENT_LIB} "../media/standby-idr-p.drc" BINARY)
if(CONFIG_TOFFEE_VIDEO_HOLD_FRAMES)
    target_add_binary_data(${COMPONENT_LIB} "../media/hold-p.drc" BINARY)
endif()
