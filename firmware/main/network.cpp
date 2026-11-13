#include "network.hpp"
#include "network_config.hpp"
#include "runtime_api.hpp"
#include "wifi_password.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs.h"
#include "lwip/inet.h"
#include <atomic>
#include <cstring>
#include <cstdio>
namespace {
constexpr char kTag[] = "network";
std::atomic<bool> g_web_ready{}, g_credentials_ready{};
char g_ap_ssid[33]{}, g_ap_password[sirokko::wifi::kPasswordCapacity]{};
bool load_wifi_password() {
    nvs_handle_t nvs{};
    if (nvs_open("sirokko", NVS_READWRITE, &nvs) != ESP_OK)
        return false;
    size_t size = sizeof(g_ap_password);
    const auto result = nvs_get_str(nvs, sirokko::wifi::kPasswordKey, g_ap_password, &size);
    nvs_close(nvs);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        g_ap_password[0] = 0;
        return true;
    }
    return result == ESP_OK && sirokko::wifi::valid_password(g_ap_password);
}
bool start_access_point() {
    if (esp_netif_init() != ESP_OK)
        return false;
    const auto loop = esp_event_loop_create_default();
    if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE)
        return false;
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!ap)
        return false;
    if (esp_netif_dhcps_stop(ap) != ESP_OK)
        return false;
    esp_netif_ip_info_t ip{};
    ip.ip.addr = ip.gw.addr = inet_addr(network_config::address);
    ip.netmask.addr = inet_addr("255.255.255.0");
    if (esp_netif_set_ip_info(ap, &ip) != ESP_OK)
        return false;
    if (esp_netif_dhcps_start(ap) != ESP_OK)
        return false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK)
        return false;
    uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK)
        return false;
    std::snprintf(g_ap_ssid, sizeof(g_ap_ssid), "Sirokko-%02X%02X%02X", mac[3], mac[4], mac[5]);
    wifi_config_t cfg{};
    std::memcpy(cfg.ap.ssid, g_ap_ssid, std::strlen(g_ap_ssid));
    cfg.ap.ssid_len = std::strlen(g_ap_ssid);
    std::memcpy(cfg.ap.password, g_ap_password, std::strlen(g_ap_password));
    cfg.ap.channel = 6;
    cfg.ap.max_connection = network_config::clients;
    cfg.ap.authmode = g_ap_password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.ap.pmf_cfg.capable = true;
    cfg.ap.pmf_cfg.required = g_ap_password[0] != 0;
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK || esp_wifi_start() != ESP_OK)
        return false;
    ESP_LOGI(kTag, "Wi-Fi AP %s (%s), %s", g_ap_ssid, g_ap_password[0] ? "password protected" : "open",
             network_config::url);
    g_credentials_ready = true;
    return true;
}
} // namespace
bool network_save_wifi_password(const char *password) {
    if (!sirokko::wifi::valid_password(password))
        return false;
    nvs_handle_t nvs{};
    if (nvs_open("sirokko", NVS_READWRITE, &nvs) != ESP_OK)
        return false;
    const bool ok =
        nvs_set_str(nvs, sirokko::wifi::kPasswordKey, password) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    // Apply on the next normal restart, without disrupting control or the connection.
    return ok;
}
bool runtime_network_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size) {
    if (!g_credentials_ready || !g_web_ready || !ssid || !password || ssid_size < sizeof(g_ap_ssid) ||
        password_size < sizeof(g_ap_password))
        return false;
    std::memcpy(ssid, g_ap_ssid, sizeof(g_ap_ssid));
    std::memcpy(password, g_ap_password, sizeof(g_ap_password));
    return true;
}
bool network_begin() {
    if (load_wifi_password() && start_access_point())
        g_web_ready = web_start(g_ap_password);
    return g_web_ready;
}
