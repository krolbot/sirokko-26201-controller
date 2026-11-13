#include "ota_verify.hpp"

#include <cstring>
#include <limits>

#if defined(ESP_PLATFORM)
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#else
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif

namespace sirokko::ota_verify {
namespace {

constexpr std::array<uint8_t, 8> kMagic{{'S', 'I', 'R', 'O', 'T', 'A', '0', '1'}};
constexpr size_t kFormatOffset = 8;
constexpr size_t kHeaderSizeOffset = 12;
constexpr size_t kImageSizeOffset = 16;
constexpr size_t kProjectOffset = 20;
constexpr size_t kTargetOffset = 52;
constexpr size_t kDigestOffset = 68;

uint32_t read_u32_be(const uint8_t *bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24U) | (static_cast<uint32_t>(bytes[1]) << 16U) |
           (static_cast<uint32_t>(bytes[2]) << 8U) | static_cast<uint32_t>(bytes[3]);
}

bool field_matches(const char *field, size_t field_size, const char *expected) {
    if (!field || !expected)
        return false;
    const size_t expected_size = std::strlen(expected);
    if (expected_size == 0 || expected_size >= field_size ||
        std::memcmp(field, expected, expected_size) != 0 || field[expected_size] != 0)
        return false;
    for (size_t index = expected_size + 1; index < field_size; ++index) {
        if (field[index] != 0)
            return false;
    }
    return true;
}

bool manifest_strings_canonical(const Manifest &manifest) {
    const auto canonical = [](const char *field, size_t size) {
        size_t end = 0;
        while (end < size && field[end] != 0) {
            const unsigned char value = static_cast<unsigned char>(field[end]);
            const bool allowed = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                                 (value >= '0' && value <= '9') || value == '_' || value == '-';
            if (!allowed)
                return false;
            ++end;
        }
        if (end == 0 || end == size)
            return false;
        for (size_t index = end + 1; index < size; ++index)
            if (field[index] != 0)
                return false;
        return true;
    };
    return canonical(manifest.project_name.data(), manifest.project_name.size()) &&
           canonical(manifest.target.data(), manifest.target.size());
}

} // namespace

Result parse_manifest(const uint8_t *bytes, size_t size, Manifest *manifest) {
    if (!bytes || !manifest)
        return Result::INPUT;
    if (size < kManifestSize)
        return Result::TRUNCATED;
    if (std::memcmp(bytes, kMagic.data(), kMagic.size()) != 0)
        return Result::MAGIC;
    if (read_u32_be(bytes + kFormatOffset) != kFormatVersion ||
        read_u32_be(bytes + kHeaderSizeOffset) != kManifestSize)
        return Result::FORMAT;
    Manifest decoded{};
    std::memcpy(decoded.signed_header.data(), bytes, decoded.signed_header.size());
    decoded.image_size = read_u32_be(bytes + kImageSizeOffset);
    std::memcpy(decoded.project_name.data(), bytes + kProjectOffset, decoded.project_name.size());
    std::memcpy(decoded.target.data(), bytes + kTargetOffset, decoded.target.size());
    std::memcpy(decoded.image_sha256.data(), bytes + kDigestOffset, decoded.image_sha256.size());
    if (!manifest_strings_canonical(decoded))
        return Result::FORMAT;
    *manifest = decoded;
    return Result::OK;
}

Result validate_manifest(const Manifest &manifest, size_t package_size, const char *expected_project,
                         const char *expected_target, size_t maximum_image_size) {
    if (!field_matches(manifest.project_name.data(), manifest.project_name.size(), expected_project))
        return Result::PROJECT;
    if (!field_matches(manifest.target.data(), manifest.target.size(), expected_target))
        return Result::TARGET;
    if (manifest.image_size == 0)
        return Result::SIZE;
    if (manifest.image_size > maximum_image_size)
        return Result::TOO_LARGE;
    const uint64_t expected_size =
        static_cast<uint64_t>(kManifestSize) + manifest.image_size + kSignatureSize;
    if (expected_size > std::numeric_limits<size_t>::max() ||
        package_size != static_cast<size_t>(expected_size)) {
        return package_size < kManifestSize ? Result::TRUNCATED : Result::SIZE;
    }
    return Result::OK;
}

Result verify_image_digest(const Manifest &manifest, const uint8_t *image, size_t image_size) {
    if (!image || image_size != manifest.image_size)
        return Result::SIZE;
    return web_protocol::digest_equal(web_protocol::sha256(image, image_size), manifest.image_sha256)
               ? Result::OK
               : Result::DIGEST;
}

Result verify_signature(const Manifest &manifest, const uint8_t *signature, size_t signature_size,
                        const uint8_t *public_key_pem, size_t public_key_pem_size) {
    if (!signature || !public_key_pem || public_key_pem_size == 0 || signature_size != kSignatureSize)
        return Result::INPUT;
#if defined(ESP_PLATFORM)
    const Digest header_digest =
        web_protocol::sha256(manifest.signed_header.data(), manifest.signed_header.size());
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    const int parsed = mbedtls_pk_parse_public_key(&key, public_key_pem, public_key_pem_size);
    const bool key_ok = parsed == 0 && mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA) &&
                        mbedtls_pk_get_bitlen(&key) == kSignatureSize * 8U;
    const int verified = key_ok ? mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, header_digest.data(),
                                                    header_digest.size(), signature, signature_size)
                                : -1;
    mbedtls_pk_free(&key);
    if (!key_ok)
        return Result::KEY;
    return verified == 0 ? Result::OK : Result::SIGNATURE;
#else
    BIO *bio = BIO_new_mem_buf(public_key_pem, static_cast<int>(public_key_pem_size));
    if (!bio)
        return Result::KEY;
    EVP_PKEY *key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!key)
        return Result::KEY;
    const bool key_ok =
        EVP_PKEY_base_id(key) == EVP_PKEY_RSA && EVP_PKEY_bits(key) == static_cast<int>(kSignatureSize * 8U);
    EVP_MD_CTX *context = key_ok ? EVP_MD_CTX_new() : nullptr;
    const bool verified =
        context && EVP_DigestVerifyInit(context, nullptr, EVP_sha256(), nullptr, key) == 1 &&
        EVP_DigestVerifyUpdate(context, manifest.signed_header.data(), manifest.signed_header.size()) == 1 &&
        EVP_DigestVerifyFinal(context, signature, signature_size) == 1;
    if (context)
        EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    if (!key_ok)
        return Result::KEY;
    return verified ? Result::OK : Result::SIGNATURE;
#endif
}

Result verify_package(const uint8_t *package, size_t package_size, const char *expected_project,
                      const char *expected_target, size_t maximum_image_size, const uint8_t *public_key_pem,
                      size_t public_key_pem_size, Manifest *manifest) {
    Manifest decoded{};
    Result result = parse_manifest(package, package_size, &decoded);
    if (result != Result::OK)
        return result;
    result = validate_manifest(decoded, package_size, expected_project, expected_target, maximum_image_size);
    if (result != Result::OK)
        return result;
    const uint8_t *image = package + kManifestSize;
    result = verify_image_digest(decoded, image, decoded.image_size);
    if (result != Result::OK)
        return result;
    const uint8_t *signature = image + decoded.image_size;
    result = verify_signature(decoded, signature, kSignatureSize, public_key_pem, public_key_pem_size);
    if (result != Result::OK)
        return result;
    if (manifest)
        *manifest = decoded;
    return Result::OK;
}

const char *result_name(Result result) {
    switch (result) {
    case Result::OK:
        return "OK";
    case Result::INPUT:
        return "INPUT";
    case Result::TRUNCATED:
        return "TRUNCATED";
    case Result::MAGIC:
        return "MAGIC";
    case Result::FORMAT:
        return "FORMAT";
    case Result::SIZE:
        return "SIZE";
    case Result::TOO_LARGE:
        return "TOO_LARGE";
    case Result::PROJECT:
        return "PROJECT";
    case Result::TARGET:
        return "TARGET";
    case Result::DIGEST:
        return "DIGEST";
    case Result::IMAGE:
        return "IMAGE";
    case Result::KEY:
        return "KEY";
    case Result::SIGNATURE:
        return "SIGNATURE";
    }
    return "UNKNOWN";
}

} // namespace sirokko::ota_verify
