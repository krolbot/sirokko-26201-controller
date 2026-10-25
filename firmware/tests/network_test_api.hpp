#pragma once
// Host boundary for the real MQTT/OTA adapter. TLS/flash/RTOS are injected; RSA/SHA use OpenSSL.
#include <algorithm>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_HTTPS_OTA_IN_PROGRESS=1, pdTRUE=1;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) (void)(p)
#define portEXIT_CRITICAL(p) (void)(p)
#define pdMS_TO_TICKS(ms) (ms)
namespace net_fake {
inline uint64_t time_us=10'000'000;
inline bool lease{}, restarted{}, tls_verified{}, finished{}, aborted{};
inline int begin_error{}, perform_error{}, finish_error{};
inline unsigned begin_calls{}, lease_calls{}, releases{}, stops{};
inline std::vector<unsigned char> image(2048,0x5a);
inline std::vector<std::string> replies;
inline sirokko::ServiceResult lease_result=sirokko::ServiceResult::OK;
struct Restart {};
}
inline int64_t esp_timer_get_time() { return net_fake::time_us; }
inline void vTaskDelay(uint32_t ms) { net_fake::time_us += ms*1000ULL; }
inline void esp_restart() { net_fake::restarted=true; throw net_fake::Restart{}; }
inline void esp_fill_random(void* p,size_t n) { std::memset(p,0xa5,n); }
struct FakeQueue { size_t size; size_t capacity; std::deque<std::vector<unsigned char>> data; };
using QueueHandle_t=FakeQueue*;
inline int xQueueSend(QueueHandle_t q,const void* p,int) {
    if(q->data.size()>=q->capacity) return 0;
    const auto* b=static_cast<const unsigned char*>(p); q->data.emplace_back(b,b+q->size); return pdTRUE;
}
using esp_mqtt_client_handle_t=void*;
using esp_event_base_t=const char*;
struct esp_mqtt_event_t { int retain{},data_len{},total_data_len{},current_data_offset{},topic_len{}; char* data{}; char* topic{}; };
constexpr int MQTT_EVENT_CONNECTED=1,MQTT_EVENT_DISCONNECTED=2,MQTT_EVENT_DATA=3;
inline int esp_mqtt_client_enqueue(void*,const char*,const char* data,int,int,int,bool) { net_fake::replies.emplace_back(data); return 1; }
inline int esp_mqtt_client_subscribe(void*,const char*,int) { return 1; }
struct esp_partition_t { uint32_t size=0x300000; };
inline esp_partition_t fake_partition;
inline const esp_partition_t* esp_ota_get_next_update_partition(void*) { return &fake_partition; }
inline int esp_partition_read(const esp_partition_t*,uint32_t offset,void* data,size_t n) {
    if(offset+n>net_fake::image.size()) return ESP_FAIL;
    std::memcpy(data,net_fake::image.data()+offset,n); return ESP_OK;
}
struct esp_app_desc_t { char version[32]="0.1.6-DRAFT"; char project_name[32]="sirokko_26201_controller"; };
inline esp_app_desc_t current_desc, incoming_desc;
inline const esp_app_desc_t* esp_app_get_description() { return &current_desc; }
struct esp_http_client_config_t { const char* url{}; const char* cert_pem{}; int timeout_ms{}; bool keep_alive_enable{},disable_auto_redirect{},skip_cert_common_name_check{}; };
struct esp_https_ota_config_t { const esp_http_client_config_t* http_config{}; };
using esp_https_ota_handle_t=void*;
inline int esp_https_ota_begin(const esp_https_ota_config_t* c,void** h) {
    ++net_fake::begin_calls;
    if(!net_fake::lease) throw std::runtime_error("flash without control lease");
    const auto& http=*c->http_config;
    net_fake::tls_verified=http.cert_pem && std::strstr(http.cert_pem,"CERTIFICATE") && !http.skip_cert_common_name_check && http.disable_auto_redirect;
    if(!net_fake::tls_verified) throw std::runtime_error("unverified TLS settings");
    *h=net_fake::begin_error ? nullptr : reinterpret_cast<void*>(1); return net_fake::begin_error;
}
inline int esp_https_ota_get_status_code(void*) { return 200; }
inline int esp_https_ota_get_image_size(void*) { return net_fake::image.size(); }
inline int esp_https_ota_get_image_len_read(void*) { return net_fake::image.size(); }
inline int esp_https_ota_get_img_desc(void*,esp_app_desc_t* d) { *d=incoming_desc; return ESP_OK; }
inline int esp_https_ota_perform(void*) { return net_fake::perform_error; }
inline bool esp_https_ota_is_complete_data_received(void*) { return net_fake::perform_error==0; }
inline int esp_https_ota_finish(void*) { net_fake::finished=true; return net_fake::finish_error; }
inline void esp_https_ota_abort(void*) { net_fake::aborted=true; }

struct mbedtls_pk_context { EVP_PKEY* key{}; };
constexpr int MBEDTLS_PK_RSA=1,MBEDTLS_MD_SHA256=2;
inline void mbedtls_pk_init(mbedtls_pk_context* p) { p->key=nullptr; }
inline int mbedtls_pk_parse_public_key(mbedtls_pk_context* p,const unsigned char* bytes,size_t n) {
    BIO* bio=BIO_new_mem_buf(bytes,n); p->key=PEM_read_bio_PUBKEY(bio,nullptr,nullptr,nullptr); BIO_free(bio); return p->key ? 0:-1;
}
inline bool mbedtls_pk_can_do(const mbedtls_pk_context* p,int) { return p->key && EVP_PKEY_is_a(p->key,"RSA"); }
inline size_t mbedtls_pk_get_bitlen(const mbedtls_pk_context* p) { return EVP_PKEY_get_bits(p->key); }
inline void mbedtls_pk_free(mbedtls_pk_context* p) { EVP_PKEY_free(p->key); }
inline int mbedtls_pk_verify(mbedtls_pk_context* p,int,const unsigned char* hash,size_t n,const unsigned char* sig,size_t len) {
    EVP_PKEY_CTX* ctx=EVP_PKEY_CTX_new(p->key,nullptr);
    int ok=EVP_PKEY_verify_init(ctx)>0 && EVP_PKEY_CTX_set_rsa_padding(ctx,RSA_PKCS1_PADDING)>0 &&
           EVP_PKEY_CTX_set_signature_md(ctx,EVP_sha256())>0 && EVP_PKEY_verify(ctx,sig,len,hash,n)>0;
    EVP_PKEY_CTX_free(ctx); return ok?0:-1;
}
inline int mbedtls_sha256(const unsigned char* p,size_t n,unsigned char* out,int) { return SHA256(p,n,out)?0:-1; }
struct mbedtls_sha256_context { EVP_MD_CTX* ctx{}; };
inline void mbedtls_sha256_init(mbedtls_sha256_context* p) { p->ctx=EVP_MD_CTX_new(); }
inline int mbedtls_sha256_starts(mbedtls_sha256_context* p,int) { return EVP_DigestInit_ex(p->ctx,EVP_sha256(),nullptr)==1?0:-1; }
inline int mbedtls_sha256_update(mbedtls_sha256_context* p,const unsigned char* b,size_t n) { return EVP_DigestUpdate(p->ctx,b,n)==1?0:-1; }
inline int mbedtls_sha256_finish(mbedtls_sha256_context* p,unsigned char* out) { return EVP_DigestFinal_ex(p->ctx,out,nullptr)==1?0:-1; }
inline void mbedtls_sha256_free(mbedtls_sha256_context* p) { EVP_MD_CTX_free(p->ctx); }
inline int mbedtls_base64_decode(unsigned char* out,size_t cap,size_t* n,const unsigned char* s,size_t len) {
    if(len%4 || cap<(len/4*3 > 2 ? len/4*3-2:0)) return -1;
    std::vector<unsigned char> temp(len+1); int decoded=EVP_DecodeBlock(temp.data(),s,len);
    if(decoded<0) return -1;
    if(len && s[len-1]=='=') --decoded;
    if(len>1 && s[len-2]=='=') --decoded;
    if(static_cast<size_t>(decoded)>cap) return -1;
    *n=decoded; std::memcpy(out,temp.data(),*n); return 0;
}
