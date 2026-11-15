#include "app/application.h"
#include "audio/audio_stream.h"
#include "gamepad/input_forwarder.h"
#include "diagnostics/memory_diagnostics.h"
#include "network/wifi_setup.h"
#include "network/connection.h"
#include "gamepad/gamepad_runtime.h"
#include "transport/usb_stream.h"
#include "video/video_stream.h"

namespace app {
void run()
{
    memory_diagnostics_start();
    uint8_t mac[6];
    wifi_setup(mac);
    usb_stream_start();
    ESP_ERROR_CHECK(video_stream_start());
    audio_stream_start();
    input_start();
    connect_gamepad(mac);
    runtime_start();
}
} // namespace app
