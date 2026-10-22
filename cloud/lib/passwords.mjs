// Password hashes: scrypt with a random salt, stored as
// `scrypt$<salt hex>$<hash hex>`, worked out off the main thread.

import crypto from 'node:crypto';
import { promisify } from 'node:util';

const scrypt = promisify(crypto.scrypt);
const OPTIONS = { N: 16384, r: 8, p: 1, maxmem: 64 * 1024 * 1024 };
const KEYLEN = 32;

export const MIN_PASSWORD = 8;

export async function hashPassword(password) {
  const salt = crypto.randomBytes(16);
  const hash = await scrypt(String(password), salt, KEYLEN, OPTIONS);
  return `scrypt$${salt.toString('hex')}$${hash.toString('hex')}`;
}

export async function verifyPassword(password, stored) {
  const [scheme, saltHex, hashHex] = String(stored ?? '').split('$');
  if (scheme !== 'scrypt' || !saltHex || !hashHex) {
    // Spend the same time as a real check, so a missing account can't be told apart.
    await scrypt(String(password), 'no-such-account', KEYLEN, OPTIONS);
    return false;
  }
  const expected = Buffer.from(hashHex, 'hex');
  const actual = await scrypt(String(password), Buffer.from(saltHex, 'hex'), expected.length, OPTIONS);
  return crypto.timingSafeEqual(expected, actual);
}

/** Why a new password won't do, or null. */
export function passwordProblem(password) {
  if (typeof password !== 'string' || password.length < MIN_PASSWORD) {
    return `A password needs at least ${MIN_PASSWORD} characters.`;
  }
  if (password.length > 200) {
    return 'That password is too long.';
  }
  return null;
}
