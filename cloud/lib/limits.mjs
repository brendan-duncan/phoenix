// Slowing down guessing: failed attempts counted per key (a login name, an
// address) in a sliding window, held in memory and capped in size.

export class Attempts {
  constructor({ max = 10, windowMs = 15 * 60 * 1000, keys = 20000 } = {}) {
    this.max = max;
    this.windowMs = windowMs;
    this.keys = keys;
    this.failures = new Map();
  }

  blocked(key, now = Date.now()) {
    const f = this.failures.get(key);
    if (!f) {
      return false;
    }
    if (now - f.first > this.windowMs) {
      this.failures.delete(key);
      return false;
    }
    return f.count >= this.max;
  }

  fail(key, now = Date.now()) {
    const f = this.failures.get(key);
    if (!f || now - f.first > this.windowMs) {
      if (this.failures.size >= this.keys) {
        this.failures.delete(this.failures.keys().next().value);
      }
      this.failures.set(key, { count: 1, first: now });
    } else {
      f.count++;
    }
  }

  clear(key) {
    this.failures.delete(key);
  }
}

/** At most `max` uses per key in a sliding window, held in memory and capped in size. */
export class Budget {
  constructor(max, windowMs, keys = 20000) {
    this.max = max;
    this.windowMs = windowMs;
    this.keys = keys;
    this.uses = new Map();
  }

  /** Whether `key` has a use left, without taking it. */
  has(key, now = Date.now()) {
    return (this.uses.get(key) ?? []).filter((t) => now - t < this.windowMs).length < this.max;
  }

  /** Takes one use for `key`; false when its budget is spent. */
  take(key, now = Date.now()) {
    const recent = (this.uses.get(key) ?? []).filter((t) => now - t < this.windowMs);
    if (recent.length >= this.max) {
      this.uses.set(key, recent);
      return false;
    }
    if (!this.uses.has(key) && this.uses.size >= this.keys) {
      this.uses.delete(this.uses.keys().next().value);
    }
    recent.push(now);
    this.uses.set(key, recent);
    return true;
  }
}

/** The client's address: the last X-Forwarded-For entry behind our own proxy. */
export function clientAddress(req, trustProxy) {
  const peer = req.socket.remoteAddress ?? '';
  if (trustProxy && (peer === '127.0.0.1' || peer === '::1' || peer === '::ffff:127.0.0.1')) {
    const forwarded = String(req.headers['x-forwarded-for'] ?? '').split(',').map((s) => s.trim()).filter(Boolean);
    if (forwarded.length) {
      return forwarded[forwarded.length - 1];
    }
  }
  return peer;
}
