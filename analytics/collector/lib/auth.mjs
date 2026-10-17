// Dashboard password: scrypt hash stored in the settings table, checked via
// HTTP Basic auth (the site is HTTPS end to end through Apache), with a
// per-IP lockout after repeated failures.

import crypto from 'node:crypto';
import { CONFIG } from './config.mjs';

const SCRYPT = { N: 16384, r: 8, p: 1, keylen: 32 };

/** @returns {string} `scrypt$<salt hex>$<hash hex>` */
export function hashPassword(password) {
  const salt = crypto.randomBytes(16);
  const hash = crypto.scryptSync(password, salt, SCRYPT.keylen, { N: SCRYPT.N, r: SCRYPT.r, p: SCRYPT.p });
  return `scrypt$${salt.toString('hex')}$${hash.toString('hex')}`;
}

export function verifyPassword(password, stored) {
  if (typeof stored !== 'string') {
    return false;
  }
  const [scheme, saltHex, hashHex] = stored.split('$');
  if (scheme !== 'scrypt' || !saltHex || !hashHex) {
    return false;
  }
  const expected = Buffer.from(hashHex, 'hex');
  const actual = crypto.scryptSync(password, Buffer.from(saltHex, 'hex'), expected.length, { N: SCRYPT.N, r: SCRYPT.r, p: SCRYPT.p });
  return crypto.timingSafeEqual(expected, actual);
}

/** @returns {{ user: string, password: string } | null} */
export function parseBasicAuth(header) {
  if (typeof header !== 'string' || !header.startsWith('Basic ')) {
    return null;
  }
  const decoded = Buffer.from(header.slice(6), 'base64').toString('utf8');
  const colon = decoded.indexOf(':');
  if (colon < 0) {
    return null;
  }
  return { user: decoded.slice(0, colon), password: decoded.slice(colon + 1) };
}

/**
 * Tracks failed logins per IP. Successful logins are cached briefly (keyed by
 * the header hash) so every dashboard API call doesn't pay for scrypt.
 */
export class AuthGate {
  constructor() {
    /** @type {Map<string, { count: number, first: number }>} */
    this.failures = new Map();
    /** @type {Map<string, number>} */
    this.okCache = new Map();
  }

  lockedOut(ip, now = Date.now()) {
    const f = this.failures.get(ip);
    if (!f) {
      return false;
    }
    if (now - f.first > CONFIG.authWindowMs) {
      this.failures.delete(ip);
      return false;
    }
    return f.count >= CONFIG.authMaxFailures;
  }

  /**
   * @param {string} ip
   * @param {string | undefined} header Authorization header
   * @param {{ user: string, hash: string } | null} creds stored credentials
   * @returns {'ok' | 'missing' | 'bad' | 'locked' | 'unset'}
   */
  check(ip, header, creds, now = Date.now()) {
    if (!creds) {
      return 'unset';
    }
    if (this.lockedOut(ip, now)) {
      return 'locked';
    }
    if (!header) {
      return 'missing';
    }
    const key = crypto.createHash('sha256').update(`${creds.hash}|${header}`).digest('hex');
    const cached = this.okCache.get(key);
    if (cached && cached > now) {
      return 'ok';
    }
    const parsed = parseBasicAuth(header);
    if (parsed && parsed.user === creds.user && verifyPassword(parsed.password, creds.hash)) {
      this.failures.delete(ip);
      if (this.okCache.size > 100) {
        this.okCache.clear();
      }
      this.okCache.set(key, now + 5 * 60 * 1000);
      return 'ok';
    }
    const f = this.failures.get(ip) ?? { count: 0, first: now };
    f.count++;
    this.failures.set(ip, f);
    if (this.failures.size > 5000) {
      this.failures.clear();
    }
    return 'bad';
  }

  /** Forget cached logins, e.g. after a password change. */
  reset() {
    this.okCache.clear();
  }
}
