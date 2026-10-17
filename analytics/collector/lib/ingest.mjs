// Turning an untrusted beacon into rows: strict payload validation, page
// classification, referrer normalization and the rate limiters that keep a
// flood from reaching the database.

import { CONFIG } from './config.mjs';

const NAME_RE = /^[a-z0-9][a-z0-9_.:-]{0,49}$/i;
const PROP_KEY_RE = /^[a-z0-9_]{1,40}$/i;
const SID_RE = /^[a-z0-9]{6,32}$/i;
const SCREEN_RE = /^\d{2,5}x\d{2,5}$/;
const LANG_RE = /^[a-z]{2,3}(-[a-z0-9]{2,8})*$/i;
// Queued events may be reported up to 10 minutes after they happened.
const MAX_AGE_MS = 10 * 60 * 1000;

/**
 * Section of the guide a normalized path belongs to (mirrors pageKind() in
 * analytics/phoenix_analytics.js).
 */
export function classifyPath(p) {
  if (p === '/') {
    return 'home';
  }
  if (p === '/get-started') {
    return 'install';
  }
  if (p === '/tutorials') {
    return 'tutorials';
  }
  if (p === '/first-animation' || p.startsWith('/tutorial-')) {
    return 'tutorial';
  }
  if (p === '/privacy') {
    return 'about';
  }
  return 'guide';
}

function clip(value, max) {
  const s = String(value);
  return s.length > max ? s.slice(0, max) : s;
}

/** Split `u` into a normalized path (no index.html / .html) and a query string. */
export function splitUrl(u) {
  let s = typeof u === 'string' ? u : '/';
  const hash = s.indexOf('#');
  if (hash >= 0) {
    s = s.slice(0, hash);
  }
  const q = s.indexOf('?');
  let p = q >= 0 ? s.slice(0, q) : s;
  const query = q >= 0 ? s.slice(q + 1) : '';
  if (!p.startsWith('/')) {
    p = `/${p}`;
  }
  p = p.replace(/\/{2,}/g, '/').replace(/\/index\.html$/, '/').replace(/\.html$/, '');
  return { path: clip(p, 300), query: clip(query, 200) };
}

/** External referrer → { host, url }; same-site or unparsable referrers → empty. */
export function normalizeReferrer(ref, siteHost) {
  if (typeof ref !== 'string' || !ref) {
    return { host: '', url: '' };
  }
  try {
    const u = new URL(ref);
    const host = u.hostname.toLowerCase().replace(/^www\./, '');
    if (host === siteHost.replace(/^www\./, '')) {
      return { host: '', url: '' };
    }
    return { host: clip(host, 120), url: clip(`${u.origin}${u.pathname}`, 200) };
  } catch {
    return { host: '', url: '' };
  }
}

function cleanProps(d) {
  if (!d || typeof d !== 'object' || Array.isArray(d)) {
    return null;
  }
  const out = {};
  let n = 0;
  for (const [k, v] of Object.entries(d)) {
    if (n >= CONFIG.maxProps || !PROP_KEY_RE.test(k)) {
      continue;
    }
    if (typeof v === 'string') {
      out[k] = clip(v, 200);
    } else if (typeof v === 'number' && Number.isFinite(v)) {
      out[k] = v;
    } else if (typeof v === 'boolean') {
      out[k] = v;
    } else {
      continue;
    }
    n++;
  }
  return n > 0 ? out : null;
}

const HINT_RE = /^[\w .()/;:-]{1,60}$/;

/** User-Agent Client Hints from Chromium clients: { b: brand, bv: version, pv: platformVersion }. */
function parseHints(ch) {
  if (!ch || typeof ch !== 'object' || Array.isArray(ch)) {
    return null;
  }
  const pick = (v) => (typeof v === 'string' && HINT_RE.test(v) ? v : '');
  const hints = { brand: pick(ch.b), version: pick(ch.bv), platformVersion: pick(ch.pv) };
  return hints.brand || hints.version || hints.platformVersion ? hints : null;
}

/**
 * Validate a decoded beacon. Returns the batch-level context plus clean events,
 * or an error string. Individual malformed events are skipped, not fatal.
 * @param {unknown} body
 * @param {number} now server time (ms)
 */
export function parsePayload(body, now) {
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    return { error: 'not an object' };
  }
  const b = /** @type {Record<string, any>} */ (body);
  if (b.v !== 1) {
    return { error: 'bad version' };
  }
  const host = typeof b.h === 'string' ? b.h.toLowerCase() : '';
  if (!CONFIG.allowedHosts.includes(host)) {
    return { error: 'host not allowed' };
  }
  if (typeof b.sid !== 'string' || !SID_RE.test(b.sid)) {
    return { error: 'bad sid' };
  }
  if (!Array.isArray(b.e) || b.e.length === 0) {
    return { error: 'no events' };
  }
  const ctx = {
    host,
    sid: b.sid,
    screen: typeof b.sc === 'string' && SCREEN_RE.test(b.sc) ? b.sc : '',
    lang: typeof b.lg === 'string' && LANG_RE.test(b.lg) ? b.lg.toLowerCase() : '',
    touch: Number.isInteger(b.tp) ? Math.max(0, Math.min(20, b.tp)) : 0,
    hints: parseHints(b.ch),
  };
  const events = [];
  for (const e of b.e.slice(0, CONFIG.maxEventsPerBatch)) {
    if (!e || typeof e !== 'object' || typeof e.n !== 'string' || !NAME_RE.test(e.n)) {
      continue;
    }
    const ago = Number.isFinite(e.a) ? Math.max(0, Math.min(MAX_AGE_MS, e.a)) : 0;
    const { path, query } = splitUrl(e.u);
    const ref = e.n === 'pageview' ? normalizeReferrer(e.r, host) : { host: '', url: '' };
    events.push({
      ts: Math.round(now - ago),
      name: e.n.toLowerCase(),
      path,
      query,
      kind: classifyPath(path),
      refHost: ref.host,
      referrer: ref.url,
      props: cleanProps(e.d),
    });
  }
  if (events.length === 0) {
    return { error: 'no valid events' };
  }
  return { ctx, events };
}

/**
 * Client address. Behind Apache, the socket peer is 127.0.0.1 and Apache has
 * APPENDED the real client to X-Forwarded-For, so the last entry is the one we
 * can trust (earlier entries are whatever the client claimed).
 */
export function clientIp(req) {
  const peer = req.socket.remoteAddress ?? '';
  const local = peer === '127.0.0.1' || peer === '::1' || peer === '::ffff:127.0.0.1';
  if (CONFIG.trustProxy && local) {
    const xff = req.headers['x-forwarded-for'];
    if (typeof xff === 'string' && xff) {
      const parts = xff.split(',');
      return parts[parts.length - 1].trim();
    }
  }
  return peer.replace(/^::ffff:/, '');
}

/** Classic token bucket. `take(n)` is false when fewer than n tokens remain. */
export class TokenBucket {
  constructor(capacity, refillPerSec, now = Date.now()) {
    this.capacity = capacity;
    this.refillPerSec = refillPerSec;
    this.tokens = capacity;
    this.last = now;
  }

  take(n = 1, now = Date.now()) {
    // Clamp: a clock stepping backwards must not drain the bucket.
    const elapsed = Math.max(0, now - this.last);
    this.tokens = Math.min(this.capacity, this.tokens + (elapsed / 1000) * this.refillPerSec);
    this.last = Math.max(this.last, now);
    if (this.tokens < n) {
      return false;
    }
    this.tokens -= n;
    return true;
  }
}

/**
 * One bucket per client IP (events/minute), plus a penalty box: an IP that
 * keeps hammering after being limited is ignored outright for a while, which
 * makes rejecting it nearly free. Memory is capped at CONFIG.maxTrackedIps.
 */
export class IpRateLimiter {
  constructor(perMinute, { penaltyStrikes = 50, penaltyMs = 15 * 60 * 1000, maxIps = CONFIG.maxTrackedIps } = {}) {
    this.perMinute = perMinute;
    this.penaltyStrikes = penaltyStrikes;
    this.penaltyMs = penaltyMs;
    this.maxIps = maxIps;
    /** @type {Map<string, { bucket: TokenBucket, strikes: number, blockedUntil: number, seen: number }>} */
    this.ips = new Map();
  }

  setRate(perMinute) {
    this.perMinute = perMinute;
    for (const entry of this.ips.values()) {
      entry.bucket.capacity = perMinute;
      entry.bucket.refillPerSec = perMinute / 60;
    }
  }

  /** @returns {'ok' | 'rate' | 'blocked'} */
  check(ip, events, now = Date.now()) {
    let entry = this.ips.get(ip);
    if (!entry) {
      if (this.ips.size >= this.maxIps) {
        this.sweep(now);
      }
      entry = { bucket: new TokenBucket(this.perMinute, this.perMinute / 60, now), strikes: 0, blockedUntil: 0, seen: now };
      this.ips.set(ip, entry);
    }
    entry.seen = now;
    if (entry.blockedUntil > now) {
      return 'blocked';
    }
    if (entry.bucket.take(events, now)) {
      entry.strikes = Math.max(0, entry.strikes - 1);
      return 'ok';
    }
    entry.strikes++;
    if (entry.strikes >= this.penaltyStrikes) {
      entry.blockedUntil = now + this.penaltyMs;
      entry.strikes = 0;
    }
    return 'rate';
  }

  /** Drop idle entries; if the map is still full (IP-rotating flood), start over. */
  sweep(now = Date.now()) {
    for (const [ip, entry] of this.ips) {
      if (now - entry.seen > 120000 && entry.blockedUntil < now) {
        this.ips.delete(ip);
      }
    }
    if (this.ips.size >= this.maxIps) {
      this.ips.clear();
    }
  }

  blockedCount(now = Date.now()) {
    let n = 0;
    for (const entry of this.ips.values()) {
      if (entry.blockedUntil > now) {
        n++;
      }
    }
    return n;
  }
}
