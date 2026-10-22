// Random ids and tokens, and the hashes tokens are stored under.

import crypto from 'node:crypto';

/** A 12-character URL-safe id for users, documents, groups, folders. */
export function newId() {
  return crypto.randomBytes(9).toString('base64url');
}

/** A 256-bit secret for sessions, invites and share links. */
export function newToken() {
  return crypto.randomBytes(32).toString('base64url');
}

export function sha256(data) {
  return crypto.createHash('sha256').update(data).digest('hex');
}

/** Tokens are stored only as hashes, so a leaked database gives no sessions. */
export function hashToken(token) {
  return sha256(`phoenix-cloud|${token}`);
}

/** Constant-time comparison of two strings. */
export function sameSecret(a, b) {
  const x = Buffer.from(String(a));
  const y = Buffer.from(String(b));
  return x.length === y.length && crypto.timingSafeEqual(x, y);
}
