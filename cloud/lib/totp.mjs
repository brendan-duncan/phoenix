// Two-step sign-in: codes from an authenticator app (TOTP, RFC 6238: HMAC-
// SHA1, 30 seconds, 6 digits, as every authenticator app makes them), and
// recovery codes for when the phone is lost.

import crypto from 'node:crypto';

const STEP_SECONDS = 30;
const ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';

export function base32Encode(bytes) {
  let bits = 0;
  let value = 0;
  let out = '';
  for (const byte of bytes) {
    value = (value << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      out += ALPHABET[(value >>> (bits - 5)) & 31];
      bits -= 5;
    }
  }
  if (bits > 0) {
    out += ALPHABET[(value << (5 - bits)) & 31];
  }
  return out;
}

export function base32Decode(text) {
  const clean = String(text).toUpperCase().replace(/[\s=-]/g, '');
  let bits = 0;
  let value = 0;
  const out = [];
  for (const ch of clean) {
    const index = ALPHABET.indexOf(ch);
    if (index < 0) {
      throw new Error('Not base32.');
    }
    value = (value << 5) | index;
    bits += 5;
    if (bits >= 8) {
      out.push((value >>> (bits - 8)) & 255);
      bits -= 8;
    }
  }
  return Buffer.from(out);
}

/** A new secret for an authenticator app (160 bits), in base32. */
export function newSecret() {
  return base32Encode(crypto.randomBytes(20));
}

/** The 30-second step `ms` falls in. */
export function stepAt(ms = Date.now()) {
  return Math.floor(ms / 1000 / STEP_SECONDS);
}

/** The 6-digit code for `secret` at `step`. */
export function codeAt(secret, step) {
  const counter = Buffer.alloc(8);
  counter.writeBigUInt64BE(BigInt(step));
  const mac = crypto.createHmac('sha1', base32Decode(secret)).update(counter).digest();
  const offset = mac[mac.length - 1] & 15;
  const number = (mac.readUInt32BE(offset) & 0x7fffffff) % 1000000;
  return String(number).padStart(6, '0');
}

/**
 * The step `code` is right for (now, or one step either side, for clocks a
 * little off), or null. A step at or before `lastStep` is refused, so a
 * code can't be used twice.
 */
export function checkCode(secret, code, lastStep = 0, ms = Date.now()) {
  const given = String(code ?? '').replace(/\s/g, '');
  if (!/^\d{6}$/.test(given)) {
    return null;
  }
  const now = stepAt(ms);
  for (const step of [now, now - 1, now + 1]) {
    if (step > lastStep && crypto.timingSafeEqual(Buffer.from(codeAt(secret, step)), Buffer.from(given))) {
      return step;
    }
  }
  return null;
}

/** The address an authenticator app reads (from the QR code) to add the account. */
export function otpauthUri({ issuer, account, secret }) {
  const label = encodeURIComponent(`${issuer}:${account}`);
  return `otpauth://totp/${label}?secret=${secret}&issuer=${encodeURIComponent(issuer)}&algorithm=SHA1&digits=6&period=${STEP_SECONDS}`;
}

/** Ten recovery codes, each used once: "abcd-efgh-jkmn". */
export function newRecoveryCodes(count = 10) {
  const letters = 'abcdefghjkmnpqrstuvwxyz23456789';
  return Array.from({ length: count }, () => {
    const bytes = crypto.randomBytes(12);
    const chars = [...bytes].map((b) => letters[b % letters.length]).join('');
    return `${chars.slice(0, 4)}-${chars.slice(4, 8)}-${chars.slice(8, 12)}`;
  });
}

/** A recovery code as typed, made comparable: lowercase, no spaces or dashes. */
export function normalizeRecoveryCode(code) {
  return String(code ?? '').toLowerCase().replace(/[^a-z0-9]/g, '');
}
