// Static configuration (environment) and the runtime-tunable settings that the
// dashboard's Admin tab edits. Settings live in the database `settings` table;
// SETTING_DEFAULTS is the fallback for keys that were never saved.

import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));

function envList(name, fallback) {
  const raw = process.env[name];
  if (!raw) {
    return fallback;
  }
  return raw.split(',').map((s) => s.trim().toLowerCase()).filter(Boolean);
}

function envInt(name, fallback) {
  const n = Number.parseInt(process.env[name] ?? '', 10);
  return Number.isFinite(n) ? n : fallback;
}

/** Fixed at startup. Override with environment variables (see phoenix-analytics.service). */
export const CONFIG = {
  host: process.env.ANALYTICS_BIND ?? '127.0.0.1',
  port: envInt('ANALYTICS_PORT', 8791),
  dataDir: path.resolve(process.env.ANALYTICS_DATA_DIR ?? path.join(HERE, '..', 'data')),
  // Hostnames a page may report from (payload `h` and the Origin header).
  allowedHosts: envList('ANALYTICS_HOSTS', ['phoenixanimate.com', 'www.phoenixanimate.com']),
  // Only trust X-Forwarded-For when the socket peer is the local Apache proxy.
  trustProxy: process.env.ANALYTICS_TRUST_PROXY !== '0',

  // Hard request limits: these bound per-request memory and CPU and are not
  // editable from the dashboard.
  maxBodyBytes: 16 * 1024,
  maxEventsPerBatch: 30,
  maxProps: 16,
  maxConnections: 256,
  // Pending-write queue. Past this, events are dropped as 'overload'.
  maxQueue: 5000,
  flushIntervalMs: 1000,
  // Rate-limiter bookkeeping is capped so an IP-rotating flood can't grow it.
  maxTrackedIps: 20000,
  // Dashboard login throttling.
  authMaxFailures: 10,
  authWindowMs: 15 * 60 * 1000,
};

/** Editable at runtime from the Admin tab. */
export const SETTING_DEFAULTS = {
  // Raw events older than this are deleted; daily rollups are kept forever.
  retentionDays: 396,
  // Stop storing raw events for the rest of the UTC day past this count.
  dailyEventCap: 200000,
  // Stop storing raw events while the database file is larger than this.
  maxDbMB: 1024,
  // Token bucket per client IP, in events per minute.
  perIpPerMinute: 120,
  // Token bucket for the whole collector, in requests per second.
  globalPerSecond: 200,
  // Token bucket for stored events across ALL IPs, per minute. The per-IP limit
  // can't stop a flood that rotates addresses; this bounds what it can write.
  globalEventsPerMinute: 3000,
  // Drop everything (counted as 'paused') without touching the database.
  ingestPaused: false,
};

/** Bounds for each setting, so a typo in the dashboard can't disable a safeguard. */
export const SETTING_LIMITS = {
  retentionDays: [1, 3650],
  dailyEventCap: [100, 10000000],
  maxDbMB: [16, 100000],
  perIpPerMinute: [5, 10000],
  globalPerSecond: [5, 10000],
  globalEventsPerMinute: [60, 1000000],
};

/**
 * Validate and coerce a partial settings object from the dashboard.
 * @returns {{ ok: true, value: Record<string, number|boolean> } | { ok: false, error: string }}
 */
export function validateSettings(input) {
  const out = {};
  for (const [key, value] of Object.entries(input ?? {})) {
    if (!(key in SETTING_DEFAULTS)) {
      return { ok: false, error: `unknown setting '${key}'` };
    }
    if (key === 'ingestPaused') {
      out[key] = value === true || value === 'true';
      continue;
    }
    const n = Number(value);
    const [lo, hi] = SETTING_LIMITS[key];
    if (!Number.isInteger(n) || n < lo || n > hi) {
      return { ok: false, error: `${key} must be an integer between ${lo} and ${hi}` };
    }
    out[key] = n;
  }
  return { ok: true, value: out };
}
