#pragma once
#include "esp_log.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <stdint.h>

static inline int udp_bind(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
        return -1;
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) != 0)
    {
        ESP_LOGE("udp", "UDP bind %u failed: %d", port, errno);
        close(fd);
        return -1;
    }
    return fd;
}
