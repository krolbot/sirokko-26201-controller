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
 * Hashes the complete OTA file. Web Crypto is preferred, but access-point HTTP
 * pages do not reliably expose crypto.subtle, so the bundled MIT fallback is
 * the authoritative fallback path.
 */
export async function sha256Hex(value, cryptoApi = globalThis.crypto) {
  const raw = asArrayBuffer(value);
  if (cryptoApi?.subtle?.digest) {
    try {
      return toHex(await cryptoApi.subtle.digest('SHA-256', raw));
    } catch (_) {
      // Fall through to the vendored, audited implementation rather than
      // accepting an unhashed upload.
    }
  }
  // The vendored UMD file can see a bundler-provided `module`; retain an
  // explicit private global as well as its normal browser `sha256` export.
  const fallback = globalThis.__sirokkoSha256 ?? globalThis.sha256;
  if (typeof fallback !== 'function') throw new Error('Локальная SHA-256 реализация недоступна');
  return fallback(raw);
}
