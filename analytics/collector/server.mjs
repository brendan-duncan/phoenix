#!/usr/bin/env node
// Phoenix analytics collector: a small, dependency-free Node service that
// stores beacons from analytics/phoenix_analytics.js (the phoenixanimate.com
// guide) in SQLite and serves a password-protected dashboard. See
// analytics/README.md.
//
//   node server.mjs                  run the collector (default command)
//   node server.mjs set-password     set the dashboard login (prompts)
//   node server.mjs update-geo       download DB-IP's city database (`update-geo country` for country only)
//   node server.mjs import-geo FILE  build geo.db from a DB-IP CSV (.csv / .csv.gz)
//   node server.mjs maintenance      roll up + apply retention once, then exit
//   node server.mjs status           print storage stats

import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';
import { AuthGate, hashPassword, verifyPassword } from './lib/auth.mjs';
import { CONFIG, validateSettings } from './lib/config.mjs';
import { GeoLookup, downloadGeo, importGeoCsv } from './lib/geo.mjs';
import { IpRateLimiter, TokenBucket, clientIp, parsePayload } from './lib/ingest.mjs';
import { Queries, filtersFromParams } from './lib/queries.mjs';
import { Store, utcDay } from './lib/store.mjs';
import { isBotUserAgent, parseUserAgent } from './lib/ua.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DASHBOARD_DIR = path.join(HERE, 'dashboard');
const DB_FILE = path.join(CONFIG.dataDir, 'analytics.db');
const GEO_FILE = path.join(CONFIG.dataDir, 'geo.db');
const HOUR_MS = 3600000;

const STATIC = {
  '/dashboard/': ['index.html', 'text/html; charset=utf-8'],
  '/dashboard/app.js': ['app.js', 'text/javascript; charset=utf-8'],
  '/dashboard/style.css': ['style.css', 'text/css; charset=utf-8'],
  // PDF export, vendored (jsPDF 4.2.1, jsPDF-AutoTable 5.0.8) and loaded only on first use.
  '/dashboard/jspdf.umd.min.js': ['jspdf.umd.min.js', 'text/javascript; charset=utf-8'],
  '/dashboard/jspdf.plugin.autotable.min.js': ['jspdf.plugin.autotable.min.js', 'text/javascript; charset=utf-8'],
};

// ── Collector ─────────────────────────────────────────────────────────────

class Collector {
  constructor(store, geo) {
    this.store = store;
    this.geo = geo;
    this.queries = new Queries(store);
    this.auth = new AuthGate();
    const s = store.settings;
    this.global = new TokenBucket(s.globalPerSecond * 2, s.globalPerSecond);
    this.globalEvents = new TokenBucket(s.globalEventsPerMinute, s.globalEventsPerMinute / 60);
    this.ips = new IpRateLimiter(s.perIpPerMinute);
    /** @type {object[]} */
    this.queue = [];
    /** @type {Map<string, number>} reason → events, not yet persisted */
    this.counts = new Map();
    /** Since process start, for the Admin tab. */
    this.sinceStart = new Map();
    this.day = utcDay(Date.now());
    this.storedToday = store.storedToday(this.day);
    this.dbFull = false;
    this.startedAt = Date.now();
    this.checkSize();
  }

  count(reason, n = 1) {
    this.counts.set(reason, (this.counts.get(reason) ?? 0) + n);
    this.sinceStart.set(reason, (this.sinceStart.get(reason) ?? 0) + n);
  }

  applySettings() {
    const s = this.store.settings;
    this.global.capacity = s.globalPerSecond * 2;
    this.global.refillPerSec = s.globalPerSecond;
    this.globalEvents.capacity = s.globalEventsPerMinute;
    this.globalEvents.refillPerSec = s.globalEventsPerMinute / 60;
    this.ips.setRate(s.perIpPerMinute);
    this.checkSize();
  }

  checkSize() {
    this.dbFull = this.store.sizeBytes() > this.store.settings.maxDbMB * 1024 * 1024;
  }

  /** Persist queued rows + counters. Runs every CONFIG.flushIntervalMs. */
  flush() {
    const today = utcDay(Date.now());
    const rows = this.queue;
    this.queue = [];
    try {
      this.store.insertEvents(rows);
      if (rows.length > 0) {
        this.count('stored', rows.length);
      }
    } catch (err) {
      console.error('[analytics] insert failed:', err.message);
      this.count('write-error', rows.length);
    }
    const counts = this.counts;
    this.counts = new Map();
    try {
      this.store.addHealth(this.day, counts);
    } catch (err) {
      console.error('[analytics] health write failed:', err.message);
    }
    if (today !== this.day) {
      this.day = today;
      this.storedToday = 0;
    }
  }

  /** Handle one beacon body. Returns the drop reason, or 'accepted'. */
  ingest(req, bodyText) {
    const now = Date.now();
    const ua = req.headers['user-agent'] ?? '';
    const ip = clientIp(req);

    let body;
    try {
      body = JSON.parse(bodyText);
    } catch {
      this.count('invalid');
      return 'invalid';
    }
    const parsed = parsePayload(body, now);
    if ('error' in parsed) {
      this.count('invalid');
      return 'invalid';
    }
    const { ctx, events } = parsed;
    const origin = req.headers.origin;
    if (typeof origin === 'string' && origin !== 'null') {
      let originHost = '';
      try {
        originHost = new URL(origin).hostname.toLowerCase();
      } catch {
        originHost = '';
      }
      if (!CONFIG.allowedHosts.includes(originHost)) {
        this.count('invalid', events.length);
        return 'invalid';
      }
    }
    if (isBotUserAgent(ua) || body.wd === 1) {
      this.count('bot', events.length);
      return 'bot';
    }
    const verdict = this.ips.check(ip, events.length, now);
    if (verdict !== 'ok') {
      this.count(verdict === 'blocked' ? 'ip-blocked' : 'ip-rate', events.length);
      return verdict;
    }
    const s = this.store.settings;
    if (s.ingestPaused) {
      this.count('paused', events.length);
      return 'paused';
    }
    if (this.dbFull) {
      this.count('db-full', events.length);
      return 'db-full';
    }
    if (this.storedToday + events.length > s.dailyEventCap) {
      this.count('daily-cap', events.length);
      return 'daily-cap';
    }
    if (!this.globalEvents.take(events.length, now)) {
      this.count('global-events', events.length);
      return 'global-events';
    }
    if (this.queue.length + events.length > CONFIG.maxQueue) {
      this.count('overload', events.length);
      return 'overload';
    }

    const agent = parseUserAgent(ua, ctx.touch, ctx.hints);
    const loc = this.geo.lookup(ip);
    for (const e of events) {
      this.queue.push({
        ...e,
        sid: ctx.sid,
        visitor: this.store.visitorHash(e.ts, ip, ua, ctx.host),
        country: loc.country,
        region: loc.region,
        city: loc.city,
        browser: agent.browser,
        browserVersion: agent.browserVersion,
        os: agent.os,
        osVersion: agent.osVersion,
        device: agent.device,
        screen: ctx.screen,
        lang: ctx.lang,
      });
    }
    this.storedToday += events.length;
    this.count('accepted', events.length);
    return 'accepted';
  }

  maintenance() {
    this.flush();
    const summary = this.store.applyRetention();
    this.checkSize();
    console.log(`[analytics] maintenance: rolled ${summary.rolled} day(s), deleted ${summary.deleted} event(s) before ${summary.cutoff}`);
    return summary;
  }
}

// ── HTTP helpers ──────────────────────────────────────────────────────────

function send(res, status, body, type = 'application/json; charset=utf-8', extra = {}) {
  const data = typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store', ...extra });
  res.end(data);
}

const DASH_HEADERS = {
  'Content-Security-Policy': "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'",
  'X-Frame-Options': 'DENY',
  'Referrer-Policy': 'no-referrer',
  'X-Content-Type-Options': 'nosniff',
};

/** Read a request body up to `limit` bytes; rejects (and stops reading) past it. */
function readBody(req, limit) {
  return new Promise((resolve, reject) => {
    const declared = Number(req.headers['content-length'] ?? 0);
    if (declared > limit) {
      reject(new Error('too large'));
      return;
    }
    const chunks = [];
    let size = 0;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > limit) {
        reject(new Error('too large'));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

function rangeFrom(params) {
  const now = Date.now();
  const to = Number(params.get('to')) || now;
  const from = Number(params.get('from')) || to - 7 * 86400000;
  if (from >= to) {
    throw new Error('from must be before to');
  }
  return { from, to };
}

function unitFor(range, requested) {
  if (requested === 'hour' || requested === 'day' || requested === 'month') {
    return requested;
  }
  const days = (range.to - range.from) / 86400000;
  return days <= 2 ? 'hour' : days <= 120 ? 'day' : 'month';
}

function csvCell(v) {
  if (v === null || v === undefined) {
    return '';
  }
  const s = String(v);
  return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
}

// ── Dashboard API ─────────────────────────────────────────────────────────

function dashboardGet(c, route, params, res) {
  const q = c.queries;
  const filters = filtersFromParams(params);
  switch (route) {
    case 'summary': {
      return send(res, 200, q.summary(rangeFrom(params), filters));
    }
    case 'series': {
      const range = rangeFrom(params);
      const unit = unitFor(range, params.get('unit'));
      return send(res, 200, { unit, points: q.series(range, filters, unit, Number(params.get('tz')) || 0) });
    }
    case 'breakdown': {
      const limit = Math.min(500, Number(params.get('limit')) || 50);
      return send(res, 200, q.breakdown(params.get('dim') ?? 'path', rangeFrom(params), filters, limit));
    }
    case 'events': {
      return send(res, 200, q.eventNames(rangeFrom(params), filters));
    }
    case 'event': {
      const range = rangeFrom(params);
      const name = params.get('name') ?? '';
      const unit = unitFor(range, params.get('unit'));
      const prop = params.get('prop');
      return send(res, 200, {
        unit,
        series: q.eventSeries(name, range, filters, unit, Number(params.get('tz')) || 0),
        props: q.eventProps(name, range, filters),
        values: prop ? q.eventValues(name, prop, range, filters) : null,
      });
    }
    case 'realtime': {
      return send(res, 200, q.realtime());
    }
    case 'downloads': {
      const range = rangeFrom(params);
      const unit = unitFor(range, params.get('unit'));
      return send(res, 200, { unit, ...q.downloads(range, filters, unit, Number(params.get('tz')) || 0) });
    }
    case 'reading': {
      return send(res, 200, q.reading(rangeFrom(params), filters));
    }
    case 'status': {
      const since = utcDay(Date.now() - 13 * 86400000);
      return send(res, 200, {
        ...c.store.status(),
        settings: c.store.settings,
        dbFull: c.dbFull,
        queue: c.queue.length,
        storedToday: c.storedToday,
        blockedIps: c.ips.blockedCount(),
        trackedIps: c.ips.ips.size,
        geo: c.geo.available,
        geoCities: c.geo.hasCities,
        uptimeSec: Math.round((Date.now() - c.startedAt) / 1000),
        rssMB: Math.round(process.memoryUsage().rss / 1048576),
        sinceStart: Object.fromEntries(c.sinceStart),
        health: c.store.health(since),
      });
    }
    case 'export': {
      const range = rangeFrom(params);
      res.writeHead(200, {
        'Content-Type': 'text/csv; charset=utf-8',
        'Content-Disposition': `attachment; filename="phoenix-events-${utcDay(range.from)}-${utcDay(range.to - 1)}.csv"`,
        'Cache-Control': 'no-store',
      });
      const cols = ['ts', 'name', 'path', 'query', 'kind', 'sid', 'visitor', 'ref_host', 'referrer', 'country',
        'region', 'city', 'browser', 'browser_ver', 'os', 'os_ver', 'device', 'screen', 'lang', 'props'];
      res.write(`${cols.join(',')}\n`);
      const stmt = c.store.db.prepare(`SELECT ${cols.join(', ')} FROM events WHERE ts >= ? AND ts < ? ORDER BY ts LIMIT 1000000`);
      for (const row of stmt.iterate(range.from, range.to)) {
        res.write(`${cols.map((k) => csvCell(k === 'ts' ? new Date(Number(row.ts)).toISOString() : row[k])).join(',')}\n`);
      }
      res.end();
      return undefined;
    }
    default:
      return send(res, 404, { error: 'not found' });
  }
}

async function dashboardPost(c, route, req, res) {
  // Browsers can't add this header cross-site without a CORS preflight, which
  // we never answer, so it blocks CSRF against the Basic-auth session.
  if (req.headers['x-phoenix-dash'] !== '1') {
    return send(res, 403, { error: 'missing x-phoenix-dash header' });
  }
  const text = await readBody(req, 16 * 1024);
  const body = text ? JSON.parse(text) : {};
  switch (route) {
    case 'settings': {
      const v = validateSettings(body);
      if (!v.ok) {
        return send(res, 400, { error: v.error });
      }
      c.store.saveSettings(v.value);
      c.applySettings();
      return send(res, 200, { settings: c.store.settings });
    }
    case 'cleanup': {
      c.flush();
      const crit = {
        before: Number.isFinite(body.before) ? body.before : undefined,
        from: Number.isFinite(body.from) ? body.from : undefined,
        to: Number.isFinite(body.to) ? body.to : undefined,
        name: typeof body.name === 'string' && body.name ? body.name : undefined,
        pathPrefix: typeof body.pathPrefix === 'string' && body.pathPrefix ? body.pathPrefix : undefined,
        alsoRollups: body.alsoRollups === true,
      };
      const result = c.store.cleanup(crit);
      c.checkSize();
      return send(res, 200, result);
    }
    case 'maintenance': {
      return send(res, 200, c.maintenance());
    }
    case 'vacuum': {
      c.flush();
      const r = c.store.vacuum();
      c.checkSize();
      return send(res, 200, r);
    }
    case 'password': {
      const creds = c.store.credentials();
      if (!creds || typeof body.current !== 'string' || !verifyPassword(body.current, creds.hash)) {
        return send(res, 400, { error: 'current password is wrong' });
      }
      if (typeof body.next !== 'string' || body.next.length < 10) {
        return send(res, 400, { error: 'new password must be at least 10 characters' });
      }
      c.store.setCredentials(creds.user, hashPassword(body.next));
      c.auth.reset();
      return send(res, 200, { ok: true });
    }
    default:
      return send(res, 404, { error: 'not found' });
  }
}

// ── Router ────────────────────────────────────────────────────────────────

function createServer(c) {
  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url ?? '/', 'http://collector');
    const p = url.pathname;
    try {
      if (p === '/collect' && req.method === 'POST') {
        if (!c.global.take(1)) {
          c.count('global-rate');
          res.writeHead(429, { Connection: 'close' });
          res.end();
          req.destroy();
          return;
        }
        let text;
        try {
          text = await readBody(req, CONFIG.maxBodyBytes);
        } catch {
          c.count('too-large');
          send(res, 413, '', 'text/plain', { Connection: 'close' });
          return;
        }
        c.ingest(req, text);
        res.writeHead(204, { 'Cache-Control': 'no-store' });
        res.end();
        return;
      }
      if (p === '/health') {
        send(res, 200, { ok: true });
        return;
      }
      if (p === '/dashboard') {
        res.writeHead(301, { Location: 'dashboard/' });
        res.end();
        return;
      }
      if (!p.startsWith('/dashboard/')) {
        send(res, 404, { error: 'not found' });
        return;
      }

      // Everything under /dashboard/ requires the password.
      const ip = clientIp(req);
      const verdict = c.auth.check(ip, req.headers.authorization, c.store.credentials());
      if (verdict === 'unset') {
        send(res, 503, 'Dashboard password not set. On the server run: node server.mjs set-password', 'text/plain; charset=utf-8');
        return;
      }
      if (verdict === 'locked') {
        send(res, 429, 'Too many failed logins. Try again in 15 minutes.', 'text/plain; charset=utf-8');
        return;
      }
      if (verdict !== 'ok') {
        send(res, 401, 'Login required', 'text/plain; charset=utf-8', { 'WWW-Authenticate': 'Basic realm="Phoenix analytics", charset="UTF-8"' });
        return;
      }

      const file = STATIC[p];
      if (file && req.method === 'GET') {
        send(res, 200, fs.readFileSync(path.join(DASHBOARD_DIR, file[0])), file[1], DASH_HEADERS);
        return;
      }
      const m = /^\/dashboard\/api\/([a-z-]+)$/.exec(p);
      if (!m) {
        send(res, 404, { error: 'not found' });
        return;
      }
      if (req.method === 'GET') {
        dashboardGet(c, m[1], url.searchParams, res);
      } else if (req.method === 'POST') {
        await dashboardPost(c, m[1], req, res);
      } else {
        send(res, 405, { error: 'method not allowed' });
      }
    } catch (err) {
      console.error('[analytics] request failed:', p, err.message);
      if (!res.headersSent) {
        send(res, p.startsWith('/dashboard/api/') ? 400 : 500, { error: err.message });
      } else {
        res.end();
      }
    }
  });
  server.maxConnections = CONFIG.maxConnections;
  server.headersTimeout = 5000;
  server.requestTimeout = 10000;
  server.keepAliveTimeout = 5000;
  server.maxHeadersCount = 60;
  return server;
}

function serve() {
  const store = new Store(DB_FILE);
  const geo = new GeoLookup(GEO_FILE);
  const c = new Collector(store, geo);
  const server = createServer(c);

  const flushTimer = setInterval(() => c.flush(), CONFIG.flushIntervalMs);
  const sizeTimer = setInterval(() => c.checkSize(), 60000);
  let geoMtime = fs.existsSync(GEO_FILE) ? fs.statSync(GEO_FILE).mtimeMs : 0;
  let lastMaintenanceDay = '';
  const hourly = () => {
    // Pick up a geo.db rebuilt by `update-geo` in another process.
    const m = fs.existsSync(GEO_FILE) ? fs.statSync(GEO_FILE).mtimeMs : 0;
    if (m !== geoMtime) {
      geoMtime = m;
      geo.open();
      console.log(`[analytics] geo database ${geo.available ? 'reloaded' : 'missing'}`);
    }
    // Rollups + retention once per UTC day (and at startup).
    const day = utcDay(Date.now());
    if (day !== lastMaintenanceDay) {
      lastMaintenanceDay = day;
      try {
        c.maintenance();
      } catch (err) {
        console.error('[analytics] maintenance failed:', err.message);
      }
    }
  };
  const hourlyTimer = setInterval(hourly, HOUR_MS);
  setTimeout(hourly, 5000);

  server.listen(CONFIG.port, CONFIG.host, () => {
    console.log(`[analytics] listening on http://${CONFIG.host}:${CONFIG.port} (data: ${CONFIG.dataDir}, geo: ${geo.available ? 'yes' : 'no'})`);
    if (!store.credentials()) {
      console.log('[analytics] dashboard disabled until you run: node server.mjs set-password');
    }
  });

  const shutdown = () => {
    clearInterval(flushTimer);
    clearInterval(sizeTimer);
    clearInterval(hourlyTimer);
    server.close();
    c.flush();
    store.close();
    geo.close();
    process.exit(0);
  };
  process.on('SIGTERM', shutdown);
  process.on('SIGINT', shutdown);
}

// ── CLI ───────────────────────────────────────────────────────────────────

function promptHidden(question) {
  return new Promise((resolve) => {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout, terminal: true });
    let muted = false;
    const write = rl._writeToOutput.bind(rl);
    rl._writeToOutput = (s) => {
      if (!muted) {
        write(s);
      }
    };
    rl.question(question, (answer) => {
      rl.close();
      process.stdout.write('\n');
      resolve(answer);
    });
    muted = true;
  });
}

async function setPassword(user) {
  const a = await promptHidden(`New dashboard password for '${user}': `);
  const b = await promptHidden('Repeat: ');
  if (a !== b) {
    console.error('Passwords do not match.');
    process.exit(1);
  }
  if (a.length < 10) {
    console.error('Use at least 10 characters.');
    process.exit(1);
  }
  const store = new Store(DB_FILE);
  store.setCredentials(user, hashPassword(a));
  store.close();
  console.log(`Password set for '${user}'. A running collector picks it up on the next login.`);
}

async function main() {
  const [cmd = 'serve', arg] = process.argv.slice(2);
  switch (cmd) {
    case 'serve':
      serve();
      break;
    case 'set-password':
      await setPassword(arg || 'admin');
      break;
    case 'update-geo': {
      // `update-geo country` for the small country-only file (~24 MB instead of ~240 MB).
      const r = await downloadGeo(GEO_FILE, arg === 'country' ? 'country' : 'city');
      console.log(`geo.db built from ${r.url}: ${r.v4} IPv4 + ${r.v6} IPv6 ranges, ${r.locations} locations`);
      break;
    }
    case 'import-geo': {
      if (!arg) {
        console.error('usage: node server.mjs import-geo <dbip-country-lite.csv[.gz]>');
        process.exit(1);
      }
      const r = await importGeoCsv(fs.createReadStream(arg), GEO_FILE, arg.endsWith('.gz'));
      console.log(`geo.db built: ${r.v4} IPv4 + ${r.v6} IPv6 ranges, ${r.locations} locations`);
      break;
    }
    case 'maintenance': {
      const store = new Store(DB_FILE);
      console.log(store.applyRetention());
      store.close();
      break;
    }
    case 'status': {
      const store = new Store(DB_FILE);
      const s = store.status();
      console.log({ ...s, sizeMB: (s.sizeBytes / 1048576).toFixed(1), settings: store.settings });
      store.close();
      break;
    }
    default:
      console.error(`unknown command '${cmd}'`);
      process.exit(1);
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
