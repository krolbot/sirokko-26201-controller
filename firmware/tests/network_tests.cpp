#define SIROKKO_NETWORK_HOST_TEST
#include "../main/service_network.cpp"
#include <iostream>
#include <functional>

bool runtime_snapshot(sirokko::UiFrame*) { return false; }
void runtime_remote_stop() { ++net_fake::stops; }
uint32_t runtime_control_passes() { return 0; }
sirokko::ServiceResult runtime_acquire_flash(FlashPurpose) {
    ++net_fake::lease_calls;
    if(net_fake::lease_result==sirokko::ServiceResult::OK) net_fake::lease=true;
    return net_fake::lease_result;
}
void runtime_release_flash() { ++net_fake::releases; net_fake::lease=false; }
namespace {
void require(bool b,const char* msg) { if(!b) throw std::runtime_error(msg); }
EVP_PKEY* key;
void setup_key() {
    EVP_PKEY_CTX* ctx=EVP_PKEY_CTX_new_from_name(nullptr,"RSA",nullptr);
    require(ctx && EVP_PKEY_keygen_init(ctx)>0 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx,3072)>0 && EVP_PKEY_generate(ctx,&key)>0,"test key generation");
    EVP_PKEY_CTX_free(ctx);
    BIO* bio=BIO_new(BIO_s_mem()); PEM_write_bio_PUBKEY(bio,key);
    char* bytes{}; long n=BIO_get_mem_data(bio,&bytes);
    require(n<static_cast<long>(sizeof(g_network.update_public_key)),"public key size");
    std::memcpy(g_network.update_public_key,bytes,n); g_network.update_public_key[n]=0; BIO_free(bio);
    std::strcpy(g_network.ca,"-----BEGIN CERTIFICATE-----\nTEST-ADAPTER-CA\n");
}
void reset() {
    net_fake::time_us=10'000'000; net_fake::lease=net_fake::restarted=net_fake::finished=net_fake::aborted=net_fake::tls_verified=false;
    net_fake::begin_error=net_fake::perform_error=net_fake::finish_error=0;
    net_fake::begin_calls=net_fake::lease_calls=net_fake::releases=net_fake::stops=0;
    net_fake::lease_result=sirokko::ServiceResult::OK; net_fake::image.assign(2048,0x5a); net_fake::replies.clear();
    fake_partition.size=0x300000; incoming_desc={}; std::strcpy(incoming_desc.version,"0.1.7-DRAFT");
    g_mqtt=reinterpret_cast<void*>(1); g_connected.store(true); g_wifi_up.store(true); g_last_sequence=0;
    std::strcpy(g_nonce,"0123456789abcdef0123456789abcdef"); std::strcpy(g_command_topic,"test/service/request");
    static FakeQueue queue{sizeof(UpdateJob),1,{}}; queue.data.clear(); g_jobs=&queue;
}
UpdateJob job() {
    UpdateJob j{}; std::strcpy(j.version,"0.1.7-DRAFT"); std::strcpy(j.url,"https://updates.example.test/image.bin");
    j.size=net_fake::image.size(); j.sequence=1; j.issued_ms=10000; j.ttl_ms=30000;
    unsigned char hash[32]{}; SHA256(net_fake::image.data(),net_fake::image.size(),hash);
    for(size_t i=0;i<32;++i) std::snprintf(j.hash+2*i,3,"%02x",hash[i]);
    return j;
}
void sign(UpdateJob& j) {
    char data[512]{};
    int n=std::snprintf(data,sizeof(data),"SIROKKO-OTA-1\nesp32s3\n%s\n3\n%s\n%u\n%s\n%s\n",kBoard,j.version,j.size,j.hash,j.url);
    EVP_MD_CTX* ctx=EVP_MD_CTX_new(); size_t size=sizeof(j.signature);
    require(EVP_DigestSignInit(ctx,nullptr,EVP_sha256(),nullptr,key)==1 &&
            EVP_DigestSign(ctx,j.signature,&size,reinterpret_cast<unsigned char*>(data),n)==1 && size==sizeof(j.signature),"sign test manifest");
    EVP_MD_CTX_free(ctx);
}
std::string packet(const std::string& operation="stop",uint32_t seq=1) {
    return "{\"op\":\""+operation+"\",\"nonce\":\""+g_nonce+"\",\"seq\":"+std::to_string(seq)+",\"uptime_ms\":10000,\"ttl_ms\":30000}";
}
void receive(std::string p,bool retained=false) {
    esp_mqtt_event_t e{}; e.retain=retained; e.data=p.data(); e.topic=g_command_topic;
    e.topic_len=std::strlen(g_command_topic); e.data_len=e.total_data_len=p.size(); accept_packet(e);
}
void run_update(const UpdateJob& j) { try { update(j); } catch(const net_fake::Restart&) {} }
void check_reply(const char* code) {
    require(!net_fake::replies.empty() && net_fake::replies.back().find(code)!=std::string::npos,"expected service reply");
}
}
int main() {
    setup_key();
    std::vector<std::pair<const char*,std::function<void()>>> cases={
        {"retained_replay_expiry_and_forbidden_commands",[]{
            receive(packet(),true); require(net_fake::stops==0,"retained STOP");
            receive(packet()); require(net_fake::stops==1,"fresh STOP");
            receive(packet()); require(net_fake::stops==1,"replay STOP");
            receive(packet("start",2)); check_reply("FORBIDDEN");
            for(const char* op:{"reset","fuel","glow","config"}) receive(packet(op,++g_last_sequence+1));
            require(net_fake::stops==1,"forbidden output controls");
            net_fake::time_us+=31'000'000; receive(packet("stop",99)); require(net_fake::stops==1,"expired STOP");
        }},
        {"reconnect_rotates_nonce_and_discards_old_commands",[]{
            std::string old=packet(); mqtt_event(nullptr,nullptr,MQTT_EVENT_DISCONNECTED,nullptr);
            mqtt_event(nullptr,nullptr,MQTT_EVENT_CONNECTED,nullptr); receive(old); require(net_fake::stops==0,"old nonce after reconnect");
            receive(packet()); require(net_fake::stops==1,"new challenge");
        }},
        {"unsigned_manifest_never_acquires_flash",[]{auto j=job(); run_update(j); check_reply("SIGNATURE"); require(net_fake::lease_calls==0 && net_fake::begin_calls==0,"unsigned flash");}},
        {"signed_manifest_metadata_is_bound",[]{auto j=job(); sign(j); require(signed_manifest(j),"valid signature"); j.hash[0]=j.hash[0]=='a'?'b':'a'; require(!signed_manifest(j),"tampered manifest");}},
        {"busy_does_not_stop_or_interrupt_combustion",[]{auto j=job(); sign(j); net_fake::lease_result=sirokko::ServiceResult::BUSY; run_update(j); check_reply("BUSY"); require(!net_fake::stops && !net_fake::begin_calls,"BUSY may not stop heater");}},
        {"oversize_and_expired_do_not_write",[]{auto j=job(); sign(j); fake_partition.size=j.size-1; run_update(j); check_reply("OVERSIZE"); require(!net_fake::lease_calls,"oversize lease"); reset(); j=job(); sign(j); j.issued_ms=0; j.ttl_ms=10; run_update(j); check_reply("EXPIRED"); require(!net_fake::lease_calls,"expired lease");}},
        {"TLS_failure_releases_lease_without_boot_selection",[]{auto j=job(); sign(j); net_fake::begin_error=-1; run_update(j); check_reply("IO"); require(net_fake::tls_verified && !net_fake::finished && !net_fake::lease && net_fake::releases==1,"TLS error leaves old app");}},
        {"truncated_disconnect_and_OTA_error_abort",[]{auto j=job(); sign(j); net_fake::perform_error=-1; run_update(j); require(net_fake::aborted && !net_fake::finished && !net_fake::lease,"truncated image selected"); reset(); j=job(); sign(j); net_fake::perform_error=ESP_ERR_HTTPS_OTA_IN_PROGRESS; g_wifi_up.store(false); run_update(j); require(net_fake::aborted && !net_fake::finished,"network loss selected image");}},
        {"wrong_image_version_and_project_abort",[]{auto j=job(); sign(j); std::strcpy(incoming_desc.version,"0.1.8-DRAFT"); run_update(j); check_reply("TARGET"); require(net_fake::aborted && !net_fake::finished,"wrong image version"); reset(); j=job(); sign(j); std::strcpy(incoming_desc.project_name,"other-board"); run_update(j); require(net_fake::aborted && !net_fake::finished,"wrong target project");}},
        {"full_image_hash_checked_before_finish",[]{auto j=job(); sign(j); net_fake::image[100]^=1; run_update(j); check_reply("HASH"); require(net_fake::aborted && !net_fake::finished && !net_fake::lease,"hash mismatch booted");}},
        {"valid_signed_OTA_keeps_exclusive_lease_through_restart",[]{auto j=job(); sign(j); run_update(j); check_reply("REBOOTING"); require(net_fake::finished && net_fake::restarted && net_fake::lease && !net_fake::releases && net_fake::tls_verified,"successful OTA invariant");}},
        {"version_hash_key_and_config_validation",[]{
            require(version_number("0.1.7-DRAFT")>version_number("0.1.6-DRAFT"),"version ordering");
            require(version_number("0.1.7-DRAFT\n")<0 && version_number("x")<0,"invalid versions");
            require(!hex_hash("ffff") && hex_hash(std::string(64,'f').c_str()),"hash format");
            g_network.schema=1; std::strcpy(g_network.ssid,"bench"); std::strcpy(g_network.username,"device"); std::strcpy(g_network.password,"test-only");
            std::strcpy(g_network.broker,"mqtts://example.test"); require(config_valid(g_network),"valid TLS config");
            std::strcpy(g_network.broker,"mqtt://example.test"); require(!config_valid(g_network),"plaintext MQTT");
            std::strcpy(g_network.broker,"mqtts://example.test"); g_network.password[0]=0; require(!config_valid(g_network),"missing authentication");
        }}
    };
    unsigned passed{};
    for(const auto& [name,test]:cases) { reset(); try { test(); ++passed; std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& e) { std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; } }
    EVP_PKEY_free(key);
    std::cout<<"RESULT "<<passed<<'/'<<cases.size()<<" passed (real adapter; injected TLS/RTOS/flash, OpenSSL RSA/SHA)\n";
    return passed==cases.size()?0:1;
}
