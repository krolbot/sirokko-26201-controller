#include "ota_verify.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::vector<uint8_t> read_bytes(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main(int argc, char** argv) {
    using namespace sirokko::ota_verify;
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s PACKAGE PUBLIC_KEY\n", argv[0]);
        return 2;
    }
    const std::vector<uint8_t> package = read_bytes(argv[1]);
    const std::vector<uint8_t> public_key = read_bytes(argv[2]);
    check(!package.empty() && !public_key.empty(), "test inputs are readable");
    if (package.empty() || public_key.empty()) return 1;

    Manifest manifest{};
    const auto verify = [&](const std::vector<uint8_t>& candidate, const char* project, const char* target,
                            size_t maximum) {
        return verify_package(candidate.data(), candidate.size(), project, target, maximum,
                              public_key.data(), public_key.size(), &manifest);
    };
    check(verify(package, "sirokko_26201_controller", "esp32s3", 0x700000) == Result::OK,
          "positive RSA-3072 signed package verifies");

    std::vector<uint8_t> altered_image = package;
    altered_image[kManifestSize] ^= 0x01U;
    check(verify(altered_image, "sirokko_26201_controller", "esp32s3", 0x700000) == Result::DIGEST,
          "altered image is rejected by signed image digest");

    std::vector<uint8_t> invalid_signature = package;
    invalid_signature.back() ^= 0x01U;
    check(verify(invalid_signature, "sirokko_26201_controller", "esp32s3", 0x700000) == Result::SIGNATURE,
          "invalid signature is rejected");

    check(verify(package, "other_project", "esp32s3", 0x700000) == Result::PROJECT,
          "wrong project is rejected before boot selection");
    check(verify(package, "sirokko_26201_controller", "esp32", 0x700000) == Result::TARGET,
          "wrong chip target is rejected before boot selection");

    check(verify(package, "sirokko_26201_controller", "esp32s3", 8) == Result::TOO_LARGE,
          "oversize image is rejected against inactive-slot bound");

    std::vector<uint8_t> truncated = package;
    truncated.pop_back();
    check(verify(truncated, "sirokko_26201_controller", "esp32s3", 0x700000) == Result::SIZE,
          "truncated package is rejected");

    std::vector<uint8_t> oversized_header = package;
    oversized_header[16] = 0x00;
    oversized_header[17] = 0x70;
    oversized_header[18] = 0x00;
    oversized_header[19] = 0x01;
    check(verify(oversized_header, "sirokko_26201_controller", "esp32s3", 0x700000) == Result::TOO_LARGE,
          "declared oversize package is rejected before signature/OTA write");

    if (failures == 0) {
        std::puts("PASS: OTA package signature, digest, project/target, oversize and truncation checks");
    }
    return failures == 0 ? 0 : 1;
}
