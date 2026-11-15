#include "network/wifi_setup.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <stdint.h>
#include <assert.h>

void wifi_setup(uint8_t ap_mac[6])
{
#if CONFIG_ZERO_BOARD
    ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_26));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, 0));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT));
#endif
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* ap = esp_netif_create_default_wifi_ap();
    assert(ap);

    // The GamePad's runtime services address the console at 192.168.1.10.
    esp_netif_ip_info_t gamepad_network = {0};
    gamepad_network.ip.addr = ESP_IP4TOADDR(192, 168, 1, 10);
    gamepad_network.gw.addr = ESP_IP4TOADDR(192, 168, 1, 10);
    gamepad_network.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap, &gamepad_network));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_country_code("IE", false));
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, ap_mac));
}
