// Bench-only on-device integration test. Not included in production SRCS.
// Invoke http_server_smoke_start() after web_start() on a USB-only DevKit.
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
bool probe(const char *path, const char *host, size_t padding, int expected) {
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return false;
    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(80);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        close(fd);
        return false;
    }
    const std::string request =
        std::string("GET ") + path + " HTTP/1.1\r\nHost: " + host +
        "\r\nUser-Agent: CaptiveNetworkSupport-test\r\nX-Test-Padding: " + std::string(padding, 'x') +
        "\r\nConnection: close\r\n\r\n";
    size_t sent = 0;
    while (sent < request.size()) {
        const int count = send(fd, request.data() + sent, request.size() - sent, 0);
        if (count <= 0)
            break;
        sent += count;
    }
    char buffer[512];
    std::string headers;
    while (headers.find("\r\n\r\n") == std::string::npos && headers.size() < 4096) {
        const int count = recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
            break;
        headers.append(buffer, count);
    }
    int status = 0;
    std::sscanf(headers.c_str(), "HTTP/1.1 %d", &status);
    const bool ok = status == expected &&
                    (expected != 302 || headers.find("Location: http://192.168.26.1/") != std::string::npos);
    // Drain the response so a successful large HTML reply does not cause a reset.
    while (recv(fd, buffer, sizeof(buffer), 0) > 0) {
    }
    close(fd);
    ESP_LOGI("http_smoke", "%s path=%s padding=%u status=%d expected=%d", ok ? "PASS" : "FAIL", path,
             static_cast<unsigned>(padding), status, expected);
    return ok;
}
void run(void *) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    unsigned passed = 0;
    passed += probe("/generate_204", "connectivitycheck.gstatic.com", 2048, 302);
    passed += probe("/hotspot-detect.html", "captive.apple.com", 4096, 302);
    passed += probe("/connecttest.txt", "www.msftconnecttest.com", 4096, 302);
    passed += probe("/", "192.168.26.1", 4096, 200);
    passed += probe("/api/state", "192.168.26.1", 4096, 200);
    passed += probe("/api/unknown", "192.168.26.1", 4096, 404);
    passed += probe("/", "192.168.26.1", 9000, 431);
    ESP_LOGI("http_smoke", "RESULT %u/7", passed);
    vTaskDelete(nullptr);
}
} // namespace
void http_server_smoke_start() {
    xTaskCreate(run, "http_smoke", 6144, nullptr, 2, nullptr);
}
