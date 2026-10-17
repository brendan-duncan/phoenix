// SQLite storage (node:sqlite, no dependencies). One file, WAL mode, small
// page cache. Raw events are deleted after `retentionDays`; before a day's raw
// events can age out it is rolled up into daily_* tables, which are kept
// forever so long-range charts survive retention.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { SETTING_DEFAULTS } from './config.mjs';

const DAY_MS = 86400000;

const SCHEMA = `
CREATE TABLE IF NOT EXISTS events (
  id INTEGER PRIMARY KEY,
  ts INTEGER NOT NULL,
  name TEXT NOT NULL,
  path TEXT NOT NULL,
  query TEXT NOT NULL DEFAULT '',
  kind TEXT NOT NULL DEFAULT '',
  sid TEXT NOT NULL,
  visitor TEXT NOT NULL,
  ref_host TEXT NOT NULL DEFAULT '',
  referrer TEXT NOT NULL DEFAULT '',
  country TEXT NOT NULL DEFAULT '',
  region TEXT NOT NULL DEFAULT '',
  city TEXT NOT NULL DEFAULT '',
  browser TEXT NOT NULL DEFAULT '',
  browser_ver TEXT NOT NULL DEFAULT '',
  os TEXT NOT NULL DEFAULT '',
  os_ver TEXT NOT NULL DEFAULT '',
  device TEXT NOT NULL DEFAULT '',
  screen TEXT NOT NULL DEFAULT '',
  lang TEXT NOT NULL DEFAULT '',
  props TEXT
);
CREATE INDEX IF NOT EXISTS ev_ts ON events (ts);
CREATE INDEX IF NOT EXISTS ev_name_ts ON events (name, ts);
CREATE INDEX IF NOT EXISTS ev_sid ON events (sid, ts);

CREATE TABLE IF NOT EXISTS daily_totals (
  day TEXT PRIMARY KEY,
  views INTEGER NOT NULL, visitors INTEGER NOT NULL, visits INTEGER NOT NULL,
  bounces INTEGER NOT NULL, duration INTEGER NOT NULL, events INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS daily_dims (
  day TEXT NOT NULL, dim TEXT NOT NULL, key TEXT NOT NULL,
  views INTEGER NOT NULL, visitors INTEGER NOT NULL,
  PRIMARY KEY (day, dim, key)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS daily_events (
  day TEXT NOT NULL, name TEXT NOT NULL, count INTEGER NOT NULL, visitors INTEGER NOT NULL,
  PRIMARY KEY (day, name)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS health (
  day TEXT NOT NULL, reason TEXT NOT NULL, count INTEGER NOT NULL,
  PRIMARY KEY (day, reason)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
`;

/**
 * Dimensions rolled up per day: SQL key expression → dashboard dimension id.
 * Region and city keys carry their parents ("US|California|San Jose") so equal
 * names in different countries stay apart.
 */
export const ROLLUP_DIMS = {
  path: 'path',
  kind: 'kind',
  ref_host: 'referrer',
  country: 'country',
  "country || '|' || region": 'region',
  "country || '|' || region || '|' || city": 'city',
  browser: 'browser',
  browser_ver: 'browserVersion',
  os: 'os',
  os_ver: 'osVersion',
  device: 'device',
  screen: 'screen',
  lang: 'lang',
};

/** Columns added after the first release, created on older databases at open. */
const ADDED_COLUMNS = ['region', 'city', 'browser_ver', 'os_ver'];

export function utcDay(ts) {
  return new Date(ts).toISOString().slice(0, 10);
}

export function dayStart(day) {
  return Date.parse(`${day}T00:00:00Z`);
}

export class Store {
  /** @param {string} file database path, or ':memory:' for tests */
  constructor(file) {
    this.file = file;
    if (file !== ':memory:') {
      fs.mkdirSync(path.dirname(file), { recursive: true });
    }
    this.db = new DatabaseSync(file);
    this.db.exec(`
      PRAGMA auto_vacuum = INCREMENTAL;
      PRAGMA journal_mode = WAL;
      PRAGMA synchronous = NORMAL;
      PRAGMA cache_size = -8000;
      PRAGMA temp_store = FILE;
      PRAGMA busy_timeout = 5000;
    `);
    this.db.exec(SCHEMA);
    const have = new Set(this.db.prepare('PRAGMA table_info(events)').all().map((c) => String(c.name)));
    for (const col of ADDED_COLUMNS) {
      if (!have.has(col)) {
        this.db.exec(`ALTER TABLE events ADD COLUMN ${col} TEXT NOT NULL DEFAULT ''`);
      }
    }
    this.insertStmt = this.db.prepare(`INSERT INTO events
      (ts, name, path, query, kind, sid, visitor, ref_host, referrer, country, region, city,
       browser, browser_ver, os, os_ver, device, screen, lang, props)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`);
    this.healthStmt = this.db.prepare(`INSERT INTO health (day, reason, count) VALUES (?, ?, ?)
      ON CONFLICT (day, reason) DO UPDATE SET count = count + excluded.count`);
    this.getSettingStmt = this.db.prepare('SELECT value FROM settings WHERE key = ?');
    this.setSettingStmt = this.db.prepare(`INSERT INTO settings (key, value) VALUES (?, ?)
      ON CONFLICT (key) DO UPDATE SET value = excluded.value`);
    this.settings = this.loadSettings();
  }

  close() {
    this.db.close();
  }

  // ── Settings ──────────────────────────────────────────────────────────

  getRaw(key) {
    const row = this.getSettingStmt.get(key);
    return row ? String(row.value) : null;
  }

  setRaw(key, value) {
    this.setSettingStmt.run(key, value);
  }

  loadSettings() {
    const out = { ...SETTING_DEFAULTS };
    for (const key of Object.keys(SETTING_DEFAULTS)) {
      const raw = this.getRaw(`setting.${key}`);
      if (raw !== null) {
        out[key] = JSON.parse(raw);
      }
    }
    return out;
  }

  /** @param {Record<string, number|boolean>} values already validated */
  saveSettings(values) {
    for (const [key, value] of Object.entries(values)) {
      this.setRaw(`setting.${key}`, JSON.stringify(value));
    }
    this.settings = this.loadSettings();
    return this.settings;
  }

  /** @returns {{ user: string, hash: string } | null} */
  credentials() {
    const hash = this.getRaw('auth.hash');
    return hash ? { user: this.getRaw('auth.user') ?? 'admin', hash } : null;
  }

  setCredentials(user, hash) {
    this.setRaw('auth.user', user);
    this.setRaw('auth.hash', hash);
  }

  // ── Visitor hashing ───────────────────────────────────────────────────

  /**
   * Daily-rotating salt: a visitor hash is stable within one UTC day and
   * unlinkable across days. Old salts are overwritten, never kept.
   */
  saltFor(day) {
    if (this.saltDay !== day) {
      const stored = this.getRaw('salt');
      const [storedDay, salt] = stored ? stored.split(' ') : [];
      if (storedDay === day && salt) {
        this.salt = salt;
      } else {
        this.salt = crypto.randomBytes(16).toString('hex');
        this.setRaw('salt', `${day} ${this.salt}`);
      }
      this.saltDay = day;
    }
    return this.salt;
  }

  visitorHash(ts, ip, ua, host) {
    const salt = this.saltFor(utcDay(ts));
    return crypto.createHash('sha256').update(`${salt}|${ip}|${ua}|${host}`).digest('hex').slice(0, 16);
  }

  // ── Writes ────────────────────────────────────────────────────────────

  /** Insert rows (from the write queue) in one transaction. */
  insertEvents(rows) {
    if (rows.length === 0) {
      return;
    }
    this.db.exec('BEGIN');
    try {
      for (const r of rows) {
        this.insertStmt.run(r.ts, r.name, r.path, r.query, r.kind, r.sid, r.visitor, r.refHost, r.referrer,
          r.country, r.region ?? '', r.city ?? '', r.browser, r.browserVersion ?? '', r.os, r.osVersion ?? '',
          r.device, r.screen, r.lang, r.props ? JSON.stringify(r.props) : null);
      }
      this.db.exec('COMMIT');
    } catch (err) {
      this.db.exec('ROLLBACK');
      throw err;
    }
  }

  /** @param {Map<string, number>} counts reason → count, for `day` */
  addHealth(day, counts) {
    if (counts.size === 0) {
      return;
    }
    this.db.exec('BEGIN');
    for (const [reason, count] of counts) {
      this.healthStmt.run(day, reason, count);
    }
    this.db.exec('COMMIT');
  }

  storedToday(day) {
    const row = this.db.prepare("SELECT count FROM health WHERE day = ? AND reason = 'stored'").get(day);
    return row ? Number(row.count) : 0;
  }

  // ── Size / status ─────────────────────────────────────────────────────

  sizeBytes() {
    const pages = Number(this.db.prepare('PRAGMA page_count').get().page_count);
    const pageSize = Number(this.db.prepare('PRAGMA page_size').get().page_size);
    let wal = 0;
    if (this.file !== ':memory:') {
      try {
        wal = fs.statSync(`${this.file}-wal`).size;
      } catch {
        wal = 0;
      }
    }
    return pages * pageSize + wal;
  }

  freeBytes() {
    const free = Number(this.db.prepare('PRAGMA freelist_count').get().freelist_count);
    const pageSize = Number(this.db.prepare('PRAGMA page_size').get().page_size);
    return free * pageSize;
  }

  status() {
    const ev = this.db.prepare('SELECT COUNT(*) n, MIN(ts) oldest, MAX(ts) newest FROM events').get();
    const days = this.db.prepare('SELECT COUNT(*) n, MIN(day) first FROM daily_totals').get();
    const names = this.db.prepare('SELECT name, COUNT(*) n FROM events GROUP BY name ORDER BY n DESC').all();
    return {
      sizeBytes: this.sizeBytes(),
      freeBytes: this.freeBytes(),
      events: Number(ev.n),
      oldest: ev.oldest === null ? null : Number(ev.oldest),
      newest: ev.newest === null ? null : Number(ev.newest),
      rollupDays: Number(days.n),
      firstRollupDay: days.first,
      eventNames: names.map((r) => ({ name: String(r.name), count: Number(r.n) })),
      lastMaintenance: this.getRaw('maintenance.last'),
    };
  }

  health(sinceDay) {
    return this.db.prepare('SELECT day, reason, count FROM health WHERE day >= ? ORDER BY day, reason').all(sinceDay)
      .map((r) => ({ day: String(r.day), reason: String(r.reason), count: Number(r.count) }));
  }

  // ── Rollups ───────────────────────────────────────────────────────────

  /** Roll up one UTC day from raw events (idempotent: replaces that day's rows). */
  rollupDay(day) {
    const t0 = dayStart(day);
    const t1 = t0 + DAY_MS;
    const db = this.db;
    db.exec('BEGIN');
    try {
      db.prepare('DELETE FROM daily_totals WHERE day = ?').run(day);
      db.prepare('DELETE FROM daily_dims WHERE day = ?').run(day);
      db.prepare('DELETE FROM daily_events WHERE day = ?').run(day);
      const t = db.prepare(`
        WITH s AS (
          SELECT sid, SUM(name = 'pageview') pv, MAX(ts) - MIN(ts) dur
          FROM events WHERE ts >= ? AND ts < ? GROUP BY sid
        )
        SELECT
          (SELECT COUNT(*) FROM events WHERE ts >= ? AND ts < ? AND name = 'pageview') views,
          (SELECT COUNT(DISTINCT visitor) FROM events WHERE ts >= ? AND ts < ? AND name = 'pageview') visitors,
          (SELECT COUNT(*) FROM events WHERE ts >= ? AND ts < ?) events,
          COUNT(*) visits, COALESCE(SUM(pv = 1), 0) bounces, COALESCE(SUM(dur), 0) duration
        FROM s WHERE pv > 0`).get(t0, t1, t0, t1, t0, t1, t0, t1);
      if (Number(t.events) > 0) {
        db.prepare('INSERT INTO daily_totals VALUES (?, ?, ?, ?, ?, ?, ?)')
          .run(day, t.views, t.visitors, t.visits, t.bounces, Math.round(Number(t.duration) / 1000), t.events);
      }
      const insDim = db.prepare('INSERT INTO daily_dims VALUES (?, ?, ?, ?, ?)');
      for (const [col, dim] of Object.entries(ROLLUP_DIMS)) {
        const rows = db.prepare(`SELECT ${col} k, COUNT(*) views, COUNT(DISTINCT visitor) visitors
          FROM events WHERE ts >= ? AND ts < ? AND name = 'pageview' GROUP BY ${col}`).all(t0, t1);
        for (const r of rows) {
          insDim.run(day, dim, String(r.k), r.views, r.visitors);
        }
      }
      const insEv = db.prepare('INSERT INTO daily_events VALUES (?, ?, ?, ?)');
      const evs = db.prepare(`SELECT name, COUNT(*) n, COUNT(DISTINCT visitor) v
        FROM events WHERE ts >= ? AND ts < ? GROUP BY name`).all(t0, t1);
      for (const r of evs) {
        insEv.run(day, r.name, r.n, r.v);
      }
      db.exec('COMMIT');
    } catch (err) {
      db.exec('ROLLBACK');
      throw err;
    }
  }

  /**
   * Roll up every complete day with raw events that isn't rolled up yet (or
   * was rolled up before more of its events arrived), up to yesterday.
   */
  rollupPending(now = Date.now()) {
    const today = utcDay(now);
    const days = this.db.prepare(`
      SELECT day FROM (
        SELECT strftime('%Y-%m-%d', ts / 1000, 'unixepoch') day, COUNT(*) n FROM events
        WHERE ts < ? GROUP BY day
      ) raw
      WHERE NOT EXISTS (SELECT 1 FROM daily_totals d WHERE d.day = raw.day AND d.events = raw.n)
      ORDER BY day`).all(dayStart(today)).map((r) => String(r.day));
    for (const day of days) {
      this.rollupDay(day);
    }
    return days;
  }

  // ── Deletion ──────────────────────────────────────────────────────────

  /**
   * Delete raw events matching `where` in chunks, so a big cleanup never holds
   * the write lock long enough to stall ingestion.
   * @returns {number} rows deleted
   */
  deleteWhere(where, params, chunk = 5000) {
    const stmt = this.db.prepare(`DELETE FROM events WHERE id IN (SELECT id FROM events WHERE ${where} LIMIT ${chunk})`);
    let total = 0;
    for (;;) {
      const res = stmt.run(...params);
      const n = Number(res.changes);
      total += n;
      if (n < chunk) {
        break;
      }
    }
    if (total > 0) {
      this.db.exec('PRAGMA incremental_vacuum(4000)');
    }
    return total;
  }

  /** Retention: roll up, then drop raw events older than retentionDays. */
  applyRetention(now = Date.now()) {
    const rolled = this.rollupPending(now);
    const cutoff = dayStart(utcDay(now - this.settings.retentionDays * DAY_MS));
    const deleted = this.deleteWhere('ts < ?', [cutoff]);
    // Health counters: 400 days is plenty.
    this.db.prepare('DELETE FROM health WHERE day < ?').run(utcDay(now - 400 * DAY_MS));
    this.db.exec('PRAGMA wal_checkpoint(TRUNCATE)');
    const summary = { at: new Date(now).toISOString(), rolled: rolled.length, deleted, cutoff: utcDay(cutoff) };
    this.setRaw('maintenance.last', JSON.stringify(summary));
    return summary;
  }

  /**
   * Admin cleanup. Every criterion is optional but at least one is required.
   * `alsoRollups` removes the matching days from daily_* too (dates only).
   * @param {{ before?: number, from?: number, to?: number, name?: string, pathPrefix?: string, alsoRollups?: boolean }} c
   */
  cleanup(c) {
    const where = [];
    const params = [];
    if (Number.isFinite(c.before)) {
      where.push('ts < ?');
      params.push(c.before);
    }
    if (Number.isFinite(c.from)) {
      where.push('ts >= ?');
      params.push(c.from);
    }
    if (Number.isFinite(c.to)) {
      where.push('ts < ?');
      params.push(c.to);
    }
    if (c.name) {
      where.push('name = ?');
      params.push(c.name);
    }
    if (c.pathPrefix) {
      where.push("path LIKE ? ESCAPE '\\'");
      params.push(`${c.pathPrefix.replace(/[\\%_]/g, (m) => `\\${m}`)}%`);
    }
    if (where.length === 0) {
      throw new Error('cleanup needs at least one criterion');
    }
    const deleted = this.deleteWhere(where.join(' AND '), params);
    let rollupsDeleted = 0;
    if (c.alsoRollups && !c.name && !c.pathPrefix) {
      const lo = Number.isFinite(c.from) ? utcDay(c.from) : '0000-00-00';
      const hiTs = Number.isFinite(c.to) ? c.to : c.before;
      const hi = Number.isFinite(hiTs) ? utcDay(hiTs) : '9999-99-99';
      for (const table of ['daily_totals', 'daily_dims', 'daily_events']) {
        rollupsDeleted += Number(this.db.prepare(`DELETE FROM ${table} WHERE day >= ? AND day < ?`).run(lo, hi).changes);
      }
    }
    return { deleted, rollupsDeleted };
  }

  /** Full VACUUM: rewrites the file to reclaim all free pages. Blocks writes while it runs. */
  vacuum() {
    const before = this.sizeBytes();
    this.db.exec('PRAGMA wal_checkpoint(TRUNCATE)');
    this.db.exec('VACUUM');
    return { before, after: this.sizeBytes() };
  }
}
