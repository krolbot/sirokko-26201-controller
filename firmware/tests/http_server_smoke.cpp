// Bench-only on-device integration test. Not included in production SRCS.
// Invoke http_server_smoke_start() after web_start() on a USB-only DevKit.
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
std::string session_cookie;
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
        "\r\nUser-Agent: Sirokko-integration-test\r\nX-Test-Padding: " + std::string(padding, 'x') + "\r\n" +
        (session_cookie.empty() ? "" : "Cookie: " + session_cookie + "\r\n") + "Connection: close\r\n\r\n";
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
    const auto cookie_start = headers.find("Set-Cookie: ");
    if (cookie_start != std::string::npos) {
        const auto begin = cookie_start + std::strlen("Set-Cookie: ");
        const auto end = headers.find(';', begin);
        if (end != std::string::npos)
            session_cookie = headers.substr(begin, end - begin);
    }
    const bool ok =
        sent == request.size() && status == expected && headers.find("Location:") == std::string::npos;
    // Drain the response so a successful large HTML reply does not cause a reset.
    while (recv(fd, buffer, sizeof(buffer), 0) > 0) {
    }
    close(fd);
    ESP_LOGI("http_smoke", "%s path=%s padding=%u status=%d expected=%d", ok ? "PASS" : "FAIL", path,
             static_cast<unsigned>(padding), status, expected);
    return ok;
}
bool rejects_unread_body(const char *method, const char *path, int expected) {
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return false;
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(80);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    const std::string request =
        std::string(method) + " " + path +
        " HTTP/1.1\r\nHost: 192.168.26.1\r\nContent-Length: 1000000\r\nCookie: " + session_cookie +
        "\r\n\r\nx"; // IDF dispatches a request with content only after its first body byte.
    const int64_t started = esp_timer_get_time();
    bool ok = connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0 &&
              send(fd, request.data(), request.size(), 0) == static_cast<int>(request.size());
    char buffer[512];
    std::string response;
    int received = -1;
    while (ok && response.size() < 4096 && (received = recv(fd, buffer, sizeof(buffer), 0)) > 0)
        response.append(buffer, received);
    const int64_t elapsed_ms = (esp_timer_get_time() - started) / 1000;
    int status{};
    std::sscanf(response.c_str(), "HTTP/1.1 %d", &status);
    ok = ok && received == 0 && status == expected && elapsed_ms < 1000 &&
         response.find("Connection: close\r\n") != std::string::npos;
    close(fd);
    ESP_LOGI("http_smoke", "%s unread body %s %s status=%d closed=%d elapsed=%lldms", ok ? "PASS" : "FAIL",
             method, path, status, received == 0, static_cast<long long>(elapsed_ms));
    return ok;
}
bool local_network_only() {
    auto *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    uint8_t dns_offer = 0xFF;
    const bool dhcp_ok = ap &&
                         esp_netif_dhcps_option(ap, ESP_NETIF_OP_GET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                                &dns_offer, sizeof(dns_offer)) == ESP_OK &&
                         dns_offer == 0;
    const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
        return false;
    timeval timeout{0, 250000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(53);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    const uint8_t query[] = {0,   1,   1,   0,   0,   1,   0,   0, 0, 0, 0, 0, 7,
                             'e', 'x', 'a', 'm', 'p', 'l', 'e', 0, 0, 1, 0, 1};
    const bool sent = sendto(fd, query, sizeof(query), 0, reinterpret_cast<sockaddr *>(&address),
                             sizeof(address)) == sizeof(query);
    uint8_t reply[64];
    const int received = recv(fd, reply, sizeof(reply), 0);
    const bool no_dns = received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED);
    close(fd);
    const bool ok = dhcp_ok && sent && no_dns;
    ESP_LOGI("http_smoke", "%s local network: DHCP DNS offer=%u DNS responder=%d", ok ? "PASS" : "FAIL",
             dns_offer, received > 0);
    return ok;
}
void run(void *) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    unsigned passed = 0;
    passed += local_network_only();
    passed += probe("/generate_204", "connectivitycheck.gstatic.com", 2048, 404);
    passed += probe("/hotspot-detect.html", "captive.apple.com", 4096, 404);
    passed += probe("/connecttest.txt", "www.msftconnecttest.com", 4096, 404);
    passed += probe("/", "192.168.26.1", 4096, 200);
    passed += probe("/api/state", "192.168.26.1", 4096, 200);
    passed += probe("/api/history", "192.168.26.1", 4096, 200);
    passed += probe("/api/unknown", "192.168.26.1", 4096, 404);
    passed += probe("/", "192.168.26.1", 9000, 431);
    passed += rejects_unread_body("GET", "/", 400);
    passed += rejects_unread_body("POST", "/api/login", 400);
    passed += rejects_unread_body("POST", "/api/challenge", 400);
    passed += rejects_unread_body("GET", "/api/state", 400);
    passed += rejects_unread_body("GET", "/api/history", 400);
    passed += rejects_unread_body("POST", "/api/command", 400);
    passed += rejects_unread_body("POST", "/api/wifi", 400);
    passed += rejects_unread_body("POST", "/api/ota", 400);
    passed += rejects_unread_body("POST", "/api/unknown", 404);
    passed += probe("/api/state", "192.168.26.1", 4096, 200);
    constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI("http_smoke", "RESULT %u/19; internal heap free=%u largest=%u minimum=%u", passed,
             static_cast<unsigned>(heap_caps_get_free_size(caps)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(caps)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(caps)));
    vTaskDelete(nullptr);
}
} // namespace
void http_server_smoke_start() {
    xTaskCreate(run, "http_smoke", 6144, nullptr, 2, nullptr);
}
