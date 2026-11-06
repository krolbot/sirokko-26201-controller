#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "web_protocol.hpp"

namespace sirokko::ota_verify {

constexpr size_t kManifestSize = 100;
constexpr size_t kSignatureSize = 384;  // RSA-3072
constexpr uint32_t kFormatVersion = 1;

using Digest = sirokko::web_protocol::Digest;

enum class Result : uint8_t {
    OK,
    INPUT,
    TRUNCATED,
    MAGIC,
    FORMAT,
    SIZE,
    TOO_LARGE,
    PROJECT,
    TARGET,
    DIGEST,
    IMAGE,
    KEY,
    SIGNATURE,
};

// Wire layout, all integers big-endian:
// magic[8], format_version[4], header_size[4], image_size[4],
// project_name[32], target[16], image_sha256[32].
// The signature follows the image and covers exactly this 100-byte header.
struct Manifest {
    uint32_t image_size{};
    std::array<char, 32> project_name{};
    std::array<char, 16> target{};
    Digest image_sha256{};
    std::array<uint8_t, kManifestSize> signed_header{};
};

Result parse_manifest(const uint8_t* bytes, size_t size, Manifest* manifest);
Result validate_manifest(const Manifest& manifest, size_t package_size, const char* expected_project,
                         const char* expected_target, size_t maximum_image_size);
Result verify_image_digest(const Manifest& manifest, const uint8_t* image, size_t image_size);
Result verify_signature(const Manifest& manifest, const uint8_t* signature, size_t signature_size,
                        const uint8_t* public_key_pem, size_t public_key_pem_size);
Result verify_package(const uint8_t* package, size_t package_size, const char* expected_project,
                      const char* expected_target, size_t maximum_image_size,
                      const uint8_t* public_key_pem, size_t public_key_pem_size, Manifest* manifest);
const char* result_name(Result result);

}  // namespace sirokko::ota_verify
