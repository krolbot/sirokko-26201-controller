#include "network_config.hpp"
#include "captive_dns.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"

namespace {
void dns_task(void *context) {
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(context));
    uint8_t packet[512];
    while (true) {
        sockaddr_in peer{};
        socklen_t peer_size = sizeof(peer);
        const int received =
            recvfrom(fd, packet, sizeof(packet), 0, reinterpret_cast<sockaddr *>(&peer), &peer_size);
        if (received < 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        const size_t reply =
            sirokko::portal::dns_reply(packet, static_cast<size_t>(received), sizeof(packet));
        if (reply)
            sendto(fd, packet, reply, 0, reinterpret_cast<sockaddr *>(&peer), peer_size);
    }
}
} // namespace
bool captive_dns_start() {
    const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
        return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(53);
    address.sin_addr.s_addr = inet_addr(network_config::address);
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        xTaskCreate(dns_task, "portal_dns", 3072, reinterpret_cast<void *>(static_cast<intptr_t>(fd)), 2,
                    nullptr) != pdPASS) {
        close(fd);
        return false;
    }
    ESP_LOGI("portal", "Captive DNS ready at %s:53", network_config::address);
    return true;
}
