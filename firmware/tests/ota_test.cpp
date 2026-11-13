#include "ota_verify.hpp"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <cassert>
#include <cstring>
#include <vector>
#include <iostream>
using namespace sirokko::ota_verify;
int main() {
    // An ephemeral test key; the production signing key is not used or stored.
    auto *generator = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    assert(generator);
    assert(EVP_PKEY_keygen_init(generator) == 1 && EVP_PKEY_CTX_set_rsa_keygen_bits(generator, 3072) == 1);
    EVP_PKEY *key = nullptr;
    assert(EVP_PKEY_keygen(generator, &key) == 1);
    EVP_PKEY_CTX_free(generator);
    BIO *pem = BIO_new(BIO_s_mem());
    assert(pem && PEM_write_bio_PUBKEY(pem, key) == 1);
    char *pem_data = nullptr;
    const auto pem_size = BIO_get_mem_data(pem, &pem_data);
    std::vector<uint8_t> package(kManifestSize + 4 + kSignatureSize);
    std::memcpy(package.data(), "SIROTA01", 8);
    auto u32 = [&](size_t offset, uint32_t value) {
        for (int i = 3; i >= 0; --i) {
            package[offset + i] = value & 255;
            value >>= 8;
        }
    };
    u32(8, 1);
    u32(12, kManifestSize);
    u32(16, 4);
    std::memcpy(package.data() + 20, "sirokko_26201_controller", 24);
    std::memcpy(package.data() + 52, "esp32s3", 8);
    package[kManifestSize] = 0xe9;
    auto digest = sirokko::web_protocol::sha256(package.data() + kManifestSize, 4);
    std::memcpy(package.data() + 68, digest.data(), digest.size());
    EVP_MD_CTX *sign = EVP_MD_CTX_new();
    assert(sign);
    assert(EVP_DigestSignInit(sign, nullptr, EVP_sha256(), nullptr, key) == 1);
    assert(EVP_DigestSignUpdate(sign, package.data(), kManifestSize) == 1);
    size_t signature_size = kSignatureSize;
    assert(EVP_DigestSignFinal(sign, package.data() + kManifestSize + 4, &signature_size) == 1);
    EVP_MD_CTX_free(sign);
    EVP_PKEY_free(key);
    auto verify = [&](size_t size, const char *target = "esp32s3") {
        return verify_package(package.data(), size, "sirokko_26201_controller", target, 1024,
                              reinterpret_cast<const uint8_t *>(pem_data), pem_size, nullptr);
    };
    assert(verify(package.size()) == Result::OK);
    assert(verify(package.size(), "esp32") == Result::TARGET);
    assert(verify(package.size() - 1) != Result::OK);
    package[kManifestSize + 1] ^= 1;
    assert(verify(package.size()) == Result::DIGEST);
    package[kManifestSize + 1] ^= 1;
    package.back() ^= 1;
    assert(verify(package.size()) == Result::SIGNATURE);
    BIO_free(pem);
    std::cout << "PASS OTA: real signature, changed image/signature, truncation, wrong target\n";
}
