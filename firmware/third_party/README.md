# Browser SHA-256 dependency

`js-sha256` **0.11.1**, MIT, Chen, Yi-Cyuan.
Source: https://raw.githubusercontent.com/emn178/js-sha256/v0.11.1/build/sha256.min.js
SHA-256 of upstream bytes: `4b53da9acab6a5d4010107be2025002fe1a22da64804dd5200b46895cb899219`.
The unmodified browser distribution is inlined between `BEGIN/END js-sha256` markers in `main/web.cpp`; that is the single shipped code copy. License: `js-sha256-LICENSE.txt`.

Used only to hash the OTA upload in browsers visiting the AP over plain HTTP, where Web Crypto `subtle` is unavailable. Firmware verifies the RSA signature independently. No CDN, network service, package manager or runtime dependency is required. Node host tests compare native and fallback SHA-256 for standard/boundary/binary vectors.
