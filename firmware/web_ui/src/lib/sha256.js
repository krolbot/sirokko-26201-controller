import '../vendor/js-sha256.js';

function asArrayBuffer(value) {
  if (value instanceof ArrayBuffer) return value;
  if (ArrayBuffer.isView(value)) return value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength);
  throw new TypeError('SHA-256 принимает ArrayBuffer или TypedArray');
}

function toHex(bytes) {
  return Array.from(new Uint8Array(bytes), (byte) => byte.toString(16).padStart(2, '0')).join('');
}

/**
 * Hash the OTA file with Web Crypto, or js-sha256 on plain HTTP.
 */
export async function sha256Hex(value, cryptoApi = globalThis.crypto) {
  const raw = asArrayBuffer(value);
  if (cryptoApi?.subtle?.digest) {
    try {
      return toHex(await cryptoApi.subtle.digest('SHA-256', raw));
    } catch (_) {
      // Web Crypto can be unavailable on the controller's HTTP origin.
    }
  }
  // The vendored UMD file can see a bundler-provided `module`; retain an
  // explicit private global as well as its normal browser `sha256` export.
  const fallback = globalThis.__sirokkoSha256 ?? globalThis.sha256;
  if (typeof fallback !== 'function') throw new Error('Локальная SHA-256 реализация недоступна');
  return fallback(raw);
}
