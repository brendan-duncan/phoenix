// Read-side queries for the dashboard. Everything takes a time range
// ({ from, to } in ms, `to` exclusive) and a filter object. Filters apply to
// raw events only; when a range reaches back past the raw retention window and
// no filters are set, totals, series and pageview breakdowns fill the older days
// from the daily rollups.

import { ROLLUP_DIMS, dayStart, utcDay } from './store.mjs';

const DAY_MS = 86400000;

const REGION_KEY = "country || '|' || region";
const CITY_KEY = "country || '|' || region || '|' || city";

/** Filterable dimensions → events column (or key expression). */
export const FILTER_COLUMNS = {
  path: 'path',
  kind: 'kind',
  country: 'country',
  region: REGION_KEY,
  city: CITY_KEY,
  browser: 'browser',
  browserVersion: 'browser_ver',
  os: 'os',
  osVersion: 'os_ver',
  device: 'device',
  screen: 'screen',
  lang: 'lang',
  referrer: 'ref_host',
};

/** Breakdown dimensions for pageviews → SQL key expression. */
const BREAKDOWN_EXPR = {
  path: 'path',
  query: "path || CASE WHEN query = '' THEN '' ELSE '?' || query END",
  kind: 'kind',
  referrer: 'ref_host',
  country: 'country',
  region: REGION_KEY,
  city: CITY_KEY,
  browser: 'browser',
  browserVersion: 'browser_ver',
  os: 'os',
  osVersion: 'os_ver',
  device: 'device',
  screen: 'screen',
  lang: 'lang',
};

const ROLLUP_DIM_IDS = new Set(Object.values(ROLLUP_DIMS));
const PROP_KEY_RE = /^[a-z0-9_]{1,40}$/i;

function num(v) {
  return v === null || v === undefined ? 0 : Number(v);
}

function percentile(sorted, p) {
  if (sorted.length === 0) {
    return null;
  }
  const i = Math.min(sorted.length - 1, Math.floor(sorted.length * p));
  return sorted[i];
}

/** Parse filters from URL search params (f.path=…, f.country=…). */
export function filtersFromParams(params) {
  const f = {};
  for (const key of Object.keys(FILTER_COLUMNS)) {
    const v = params.get(`f.${key}`);
    if (v !== null) {
      f[key] = v;
    }
  }
  return f;
}

export class Queries {
  /** @param {import('./store.mjs').Store} store */
  constructor(store) {
    this.store = store;
    this.db = store.db;
  }

  /**
   * WHERE clause for a range + filters. Referrer filters are session-level
   * (only a session's landing pageview carries the referrer).
   */
  where(range, filters, extra = '') {
    const parts = ['ts >= ?', 'ts < ?'];
    const params = [range.from, range.to];
    for (const [key, value] of Object.entries(filters ?? {})) {
      const col = FILTER_COLUMNS[key];
      if (!col) {
        continue;
      }
      if (key === 'referrer') {
        parts.push('sid IN (SELECT sid FROM events WHERE ts >= ? AND ts < ? AND ref_host = ?)');
        params.push(range.from - DAY_MS, range.to, value);
      } else {
        parts.push(`${col} = ?`);
        params.push(value);
      }
    }
    if (extra) {
      parts.push(extra);
    }
    return { sql: parts.join(' AND '), params };
  }

  /** Earliest raw event timestamp (start of raw coverage), or null. */
  rawStart() {
    const row = this.db.prepare('SELECT MIN(ts) t FROM events').get();
    return row.t === null ? null : Number(row.t);
  }

  /** Range split into a rollup part (whole UTC days before raw coverage) and a raw part. */
  split(range, filters) {
    const hasFilters = Object.keys(filters ?? {}).length > 0;
    if (hasFilters) {
      return { rollup: null, raw: range };
    }
    // No raw events at all (everything aged out): the whole range is rollups.
    const raw0 = this.rawStart() ?? range.to;
    if (range.from >= raw0) {
      return { rollup: null, raw: range };
    }
    const cut = Math.min(range.to, dayStart(utcDay(raw0)));
    if (cut <= range.from) {
      return { rollup: null, raw: range };
    }
    return {
      rollup: { fromDay: utcDay(range.from), toDay: utcDay(cut - 1) },
      raw: { from: cut, to: range.to },
    };
  }

  totalsRaw(range, filters) {
    const w = this.where(range, filters);
    const row = this.db.prepare(`
      WITH s AS (
        SELECT sid, SUM(name = 'pageview') pv, MAX(ts) - MIN(ts) dur
        FROM events WHERE ${w.sql} GROUP BY sid
      )
      SELECT COUNT(*) visits, COALESCE(SUM(pv), 0) views, COALESCE(SUM(pv = 1), 0) bounces,
             COALESCE(SUM(dur), 0) dur
      FROM s WHERE pv > 0`).get(...w.params);
    const v = this.db.prepare(`SELECT COUNT(DISTINCT visitor) n FROM events WHERE ${w.sql} AND name = 'pageview'`)
      .get(...w.params);
    const d = this.db.prepare(`SELECT COUNT(*) n FROM events WHERE ${w.sql} AND name = 'download'`).get(...w.params);
    return {
      views: num(row.views),
      visitors: num(v.n),
      visits: num(row.visits),
      bounces: num(row.bounces),
      durationSec: Math.round(num(row.dur) / 1000),
      downloads: num(d.n),
    };
  }

  totals(range, filters) {
    const parts = this.split(range, filters);
    const t = this.totalsRaw(parts.raw, filters);
    if (parts.rollup) {
      const r = this.db.prepare(`SELECT SUM(views) views, SUM(visitors) visitors, SUM(visits) visits,
        SUM(bounces) bounces, SUM(duration) duration FROM daily_totals WHERE day >= ? AND day <= ?`)
        .get(parts.rollup.fromDay, parts.rollup.toDay);
      t.views += num(r.views);
      t.visitors += num(r.visitors);
      t.visits += num(r.visits);
      t.bounces += num(r.bounces);
      t.durationSec += num(r.duration);
      const d = this.db.prepare(`SELECT SUM(count) n FROM daily_events WHERE name = 'download' AND day >= ? AND day <= ?`)
        .get(parts.rollup.fromDay, parts.rollup.toDay);
      t.downloads += num(d.n);
    }
    return {
      ...t,
      bounceRate: t.visits > 0 ? t.bounces / t.visits : 0,
      avgVisitSec: t.visits > 0 ? t.durationSec / t.visits : 0,
    };
  }

  /** Current period and the equally long period just before it. */
  summary(range, filters) {
    const len = range.to - range.from;
    return {
      current: this.totals(range, filters),
      previous: this.totals({ from: range.from - len, to: range.from }, filters),
    };
  }

  /**
   * Views + visitors per bucket. `unit` is hour, day or month; `tzMin` is the
   * viewer's UTC offset in minutes so buckets follow their local calendar.
   */
  series(range, filters, unit, tzMin = 0) {
    const off = tzMin * 60000;
    const key = unit === 'hour'
      ? `strftime('%Y-%m-%d %H:00', (ts + ${off}) / 1000, 'unixepoch')`
      : unit === 'month'
        ? `strftime('%Y-%m', (ts + ${off}) / 1000, 'unixepoch')`
        : `strftime('%Y-%m-%d', (ts + ${off}) / 1000, 'unixepoch')`;
    const parts = unit === 'hour' ? { rollup: null, raw: range } : this.split(range, filters);
    const w = this.where(parts.raw, filters, "name = 'pageview'");
    const rows = this.db.prepare(`SELECT ${key} k, COUNT(*) views, COUNT(DISTINCT visitor) visitors
      FROM events WHERE ${w.sql} GROUP BY k`).all(...w.params);
    const out = new Map(rows.map((r) => [String(r.k), { views: num(r.views), visitors: num(r.visitors) }]));
    if (parts.rollup) {
      const rk = unit === 'month' ? 'substr(day, 1, 7)' : 'day';
      const rr = this.db.prepare(`SELECT ${rk} k, SUM(views) views, SUM(visitors) visitors FROM daily_totals
        WHERE day >= ? AND day <= ? GROUP BY k`).all(parts.rollup.fromDay, parts.rollup.toDay);
      for (const r of rr) {
        const cur = out.get(String(r.k)) ?? { views: 0, visitors: 0 };
        cur.views += num(r.views);
        cur.visitors += num(r.visitors);
        out.set(String(r.k), cur);
      }
    }
    return [...out.entries()].sort((a, b) => (a[0] < b[0] ? -1 : 1)).map(([k, v]) => ({ t: k, ...v }));
  }

  /** Top values of a pageview dimension (plus entry/exit pages). */
  breakdown(dim, range, filters, limit = 50) {
    if (dim === 'entry' || dim === 'exit') {
      const w = this.where(range, filters, "name = 'pageview'");
      const order = dim === 'entry' ? 'ts ASC' : 'ts DESC';
      return this.db.prepare(`
        SELECT path k, COUNT(*) views, COUNT(DISTINCT visitor) visitors FROM (
          SELECT path, visitor, ROW_NUMBER() OVER (PARTITION BY sid ORDER BY ${order}) rn
          FROM events WHERE ${w.sql}
        ) WHERE rn = 1 GROUP BY k ORDER BY views DESC LIMIT ?`).all(...w.params, limit)
        .map((r) => ({ key: String(r.k), views: num(r.views), visitors: num(r.visitors) }));
    }
    const expr = BREAKDOWN_EXPR[dim];
    if (!expr) {
      throw new Error(`unknown dimension '${dim}'`);
    }
    const parts = dim !== 'query' && ROLLUP_DIM_IDS.has(dim) ? this.split(range, filters) : { rollup: null, raw: range };
    const w = this.where(parts.raw, filters, "name = 'pageview'");
    const rows = this.db.prepare(`SELECT ${expr} k, COUNT(*) views, COUNT(DISTINCT visitor) visitors
      FROM events WHERE ${w.sql} GROUP BY k`).all(...w.params);
    const out = new Map(rows.map((r) => [String(r.k), { views: num(r.views), visitors: num(r.visitors) }]));
    if (parts.rollup) {
      const rr = this.db.prepare(`SELECT key k, SUM(views) views, SUM(visitors) visitors FROM daily_dims
        WHERE dim = ? AND day >= ? AND day <= ? GROUP BY key`).all(dim, parts.rollup.fromDay, parts.rollup.toDay);
      for (const r of rr) {
        const cur = out.get(String(r.k)) ?? { views: 0, visitors: 0 };
        cur.views += num(r.views);
        cur.visitors += num(r.visitors);
        out.set(String(r.k), cur);
      }
    }
    return [...out.entries()]
      .map(([key, v]) => ({ key, ...v }))
      .sort((a, b) => b.views - a.views)
      .slice(0, limit);
  }

  // ── Custom events ─────────────────────────────────────────────────────

  eventNames(range, filters) {
    const w = this.where(range, filters, "name <> 'pageview'");
    return this.db.prepare(`SELECT name, COUNT(*) n, COUNT(DISTINCT visitor) v FROM events
      WHERE ${w.sql} GROUP BY name ORDER BY n DESC`).all(...w.params)
      .map((r) => ({ name: String(r.name), count: num(r.n), visitors: num(r.v) }));
  }

  eventSeries(name, range, filters, unit, tzMin = 0) {
    const off = tzMin * 60000;
    const fmt = unit === 'hour' ? '%Y-%m-%d %H:00' : unit === 'month' ? '%Y-%m' : '%Y-%m-%d';
    const w = this.where(range, filters, 'name = ?');
    return this.db.prepare(`SELECT strftime('${fmt}', (ts + ${off}) / 1000, 'unixepoch') k, COUNT(*) n
      FROM events WHERE ${w.sql} GROUP BY k ORDER BY k`).all(...w.params, name)
      .map((r) => ({ t: String(r.k), count: num(r.n) }));
  }

  eventProps(name, range, filters) {
    const w = this.where(range, filters, 'name = ? AND props IS NOT NULL');
    return this.db.prepare(`SELECT j.key k, COUNT(*) n FROM events, json_each(events.props) j
      WHERE ${w.sql} GROUP BY j.key ORDER BY n DESC`).all(...w.params, name)
      .map((r) => ({ key: String(r.k), count: num(r.n) }));
  }

  /** Value distribution of one property; numeric properties also get percentiles. */
  eventValues(name, prop, range, filters, limit = 50) {
    if (!PROP_KEY_RE.test(prop)) {
      throw new Error('bad property name');
    }
    const w = this.where(range, filters, 'name = ?');
    const jp = `json_extract(props, '$.${prop}')`;
    const values = this.db.prepare(`SELECT ${jp} val, COUNT(*) n, COUNT(DISTINCT visitor) v FROM events
      WHERE ${w.sql} AND ${jp} IS NOT NULL GROUP BY val ORDER BY n DESC LIMIT ?`).all(...w.params, name, limit)
      .map((r) => ({ value: r.val, count: num(r.n), visitors: num(r.v) }));
    let stats = null;
    const numeric = this.db.prepare(`SELECT ${jp} val FROM events WHERE ${w.sql}
      AND json_type(props, '$.${prop}') IN ('integer', 'real') ORDER BY val LIMIT 50000`).all(...w.params, name)
      .map((r) => Number(r.val));
    if (numeric.length > 0) {
      const sum = numeric.reduce((a, b) => a + b, 0);
      stats = {
        n: numeric.length,
        min: numeric[0],
        p50: percentile(numeric, 0.5),
        p95: percentile(numeric, 0.95),
        max: numeric[numeric.length - 1],
        avg: sum / numeric.length,
      };
    }
    return { values, stats };
  }

  // ── Realtime ──────────────────────────────────────────────────────────

  realtime(now = Date.now()) {
    const from = now - 30 * 60000;
    const range = { from, to: now + 60000 };
    const t = this.db.prepare(`SELECT COUNT(DISTINCT visitor) visitors, SUM(name = 'pageview') views, COUNT(*) events
      FROM events WHERE ts >= ?`).get(from);
    // CAST: node:sqlite binds JS numbers as REAL, which would make this a
    // fractional division.
    const perMinute = this.db.prepare(`SELECT CAST((ts - ?) / 60000 AS INTEGER) m, SUM(name = 'pageview') views,
      COUNT(DISTINCT visitor) visitors FROM events WHERE ts >= ? GROUP BY m ORDER BY m`).all(from, from)
      .map((r) => ({ m: num(r.m), views: num(r.views), visitors: num(r.visitors) }));
    const recent = this.db.prepare(`SELECT ts, name, path, country, browser, os, device, props FROM events
      WHERE ts >= ? ORDER BY ts DESC LIMIT 60`).all(from)
      .map((r) => ({ ts: num(r.ts), name: String(r.name), path: String(r.path), country: String(r.country),
        browser: String(r.browser), os: String(r.os), device: String(r.device), props: r.props ? JSON.parse(String(r.props)) : null }));
    return {
      visitors: num(t.visitors),
      views: num(t.views),
      events: num(t.events),
      perMinute,
      pages: this.breakdown('path', range, {}, 10),
      countries: this.breakdown('country', range, {}, 10),
      recent,
    };
  }

  // ── Custom-event helpers ──────────────────────────────────────────────

  /** Counts of one property's values for one event name. */
  propCounts(name, prop, range, filters, extra = '', limit = 50) {
    if (!PROP_KEY_RE.test(prop)) {
      throw new Error('bad property name');
    }
    const w = this.where(range, filters, `name = ?${extra ? ` AND ${extra}` : ''}`);
    return this.db.prepare(`SELECT json_extract(props, '$.${prop}') k, COUNT(*) n, COUNT(DISTINCT visitor) v
      FROM events WHERE ${w.sql} GROUP BY k ORDER BY n DESC LIMIT ?`).all(...w.params, name, limit)
      .map((r) => ({ key: r.k === null ? '(none)' : String(r.k), count: num(r.n), visitors: num(r.v) }));
  }

  // ── Guide reading ─────────────────────────────────────────────────────

  reading(range, filters) {
    const w = this.where(range, filters);
    const pages = this.db.prepare(`SELECT path,
        SUM(name = 'pageview') views,
        COUNT(DISTINCT CASE WHEN name = 'pageview' THEN visitor END) visitors,
        COUNT(DISTINCT CASE WHEN name = 'read-depth' AND json_extract(props, '$.depth') >= 50 THEN sid END) d50,
        COUNT(DISTINCT CASE WHEN name = 'read-depth' AND json_extract(props, '$.depth') >= 100 THEN sid END) d100,
        COUNT(DISTINCT CASE WHEN name = 'read-complete' THEN sid END) completes,
        AVG(CASE WHEN name = 'time-on-page' THEN json_extract(props, '$.seconds') END) avgSec,
        AVG(CASE WHEN name = 'time-on-page' THEN json_extract(props, '$.depth') END) avgDepth,
        SUM(name = 'code-copy') copies
      FROM events WHERE ${w.sql} GROUP BY path HAVING views > 0 ORDER BY views DESC LIMIT 200`).all(...w.params)
      .map((r) => ({ path: String(r.path), views: num(r.views), visitors: num(r.visitors), d50: num(r.d50),
        d100: num(r.d100), completes: num(r.completes), avgSec: r.avgSec === null ? null : Number(r.avgSec),
        avgDepth: r.avgDepth === null ? null : Number(r.avgDepth), copies: num(r.copies) }));

    // Headings reached, per page: visits that brought each heading into view.
    const wh = this.where(range, filters, "name = 'section'");
    const sections = this.db.prepare(`SELECT path, json_extract(props, '$.id') hid, MIN(json_extract(props, '$.n')) n,
        MAX(json_extract(props, '$.level')) level, COUNT(DISTINCT sid) visits
      FROM events WHERE ${wh.sql} GROUP BY path, hid ORDER BY path, n LIMIT 2000`).all(...wh.params)
      .map((r) => ({ path: String(r.path), id: r.hid === null ? '' : String(r.hid), n: num(r.n),
        level: num(r.level) || 2, visits: num(r.visits) }));

    // The search box: how often it is used and whether it finds something.
    // The query text is never recorded, only its length.
    const ws = this.where(range, filters, "name = 'search'");
    const s = this.db.prepare(`SELECT COUNT(*) n, COUNT(DISTINCT visitor) v,
        SUM(json_extract(props, '$.chosen') = 1) chosen,
        SUM(json_extract(props, '$.results') = 0) zero,
        AVG(CASE WHEN json_extract(props, '$.results') >= 0 THEN json_extract(props, '$.results') END) avgResults,
        AVG(json_extract(props, '$.len')) avgLen,
        AVG(json_extract(props, '$.rank')) avgRank
      FROM events WHERE ${ws.sql}`).get(...ws.params);
    const searchFrom = this.db.prepare(`SELECT path k, COUNT(*) n, SUM(json_extract(props, '$.chosen') = 1) chosen
      FROM events WHERE ${ws.sql} GROUP BY path ORDER BY n DESC LIMIT 30`).all(...ws.params)
      .map((r) => ({ key: String(r.k), count: num(r.n), chosen: num(r.chosen) }));

    const wt = this.where(range, filters, "name = 'tutorial-open'");
    const tutorials = this.db.prepare(`SELECT json_extract(props, '$.tutorial') k, COUNT(*) n, COUNT(DISTINCT visitor) v,
        SUM(json_extract(props, '$.from') = '/tutorials') fromIndex
      FROM events WHERE ${wt.sql} GROUP BY k ORDER BY n DESC LIMIT 100`).all(...wt.params)
      .map((r) => ({ key: r.k === null ? '(none)' : String(r.k), count: num(r.n), visitors: num(r.v), fromIndex: num(r.fromIndex) }));

    return {
      pages,
      sections,
      search: {
        count: num(s.n),
        visitors: num(s.v),
        chosen: num(s.chosen),
        zero: num(s.zero),
        avgResults: s.avgResults === null ? null : Number(s.avgResults),
        avgLen: s.avgLen === null ? null : Number(s.avgLen),
        avgRank: s.avgRank === null ? null : Number(s.avgRank),
        from: searchFrom,
      },
      tutorials,
      tutorialFrom: this.propCounts('tutorial-open', 'from', range, filters),
      themes: this.propCounts('theme', 'to', range, filters),
    };
  }

  // ── Downloads and outbound links ──────────────────────────────────────

  /**
   * App downloads (`download`: installers and portable builds) and lesson
   * files (`lesson-download`), plus outbound links. Raw events only, so the
   * range is bounded by retention.
   */
  downloads(range, filters, unit = 'day', tzMin = 0) {
    const w = this.where(range, filters, "name = 'download'");
    const t = this.db.prepare(`SELECT COUNT(*) n, COUNT(DISTINCT visitor) v FROM events WHERE ${w.sql}`).get(...w.params);
    const wl = this.where(range, filters, "name = 'lesson-download'");
    const lessons = this.db.prepare(`SELECT COUNT(*) n FROM events WHERE ${wl.sql}`).get(...wl.params);
    const visitors = this.totalsRaw(range, filters).visitors;
    const off = tzMin * 60000;
    const fmt = unit === 'hour' ? '%Y-%m-%d %H:00' : unit === 'month' ? '%Y-%m' : '%Y-%m-%d';
    const wb = this.where(range, filters, "name IN ('download', 'lesson-download')");
    const series = this.db.prepare(`SELECT strftime('${fmt}', (ts + ${off}) / 1000, 'unixepoch') k,
        SUM(name = 'download') apps, SUM(name = 'lesson-download') lessons
      FROM events WHERE ${wb.sql} GROUP BY k ORDER BY k`).all(...wb.params)
      .map((r) => ({ t: String(r.k), apps: num(r.apps), lessons: num(r.lessons) }));
    // Pages the download links were clicked on.
    const pages = this.db.prepare(`SELECT path k, COUNT(*) n, COUNT(DISTINCT visitor) v FROM events
      WHERE ${w.sql} GROUP BY k ORDER BY n DESC LIMIT 50`).all(...w.params)
      .map((r) => ({ key: String(r.k), count: num(r.n), visitors: num(r.v) }));
    // The visitor's own OS (from the user agent) against the OS of the file they took.
    const visitorOs = this.db.prepare(`SELECT os k, json_extract(props, '$.os') fos, COUNT(*) n, COUNT(DISTINCT visitor) v
      FROM events WHERE ${w.sql} GROUP BY k, fos ORDER BY n DESC LIMIT 50`).all(...w.params)
      .map((r) => ({ key: String(r.k), fileOs: r.fos === null ? '' : String(r.fos), count: num(r.n), visitors: num(r.v) }));
    return {
      tiles: {
        downloads: num(t.n),
        downloaders: num(t.v),
        lessonFiles: num(lessons.n),
        visitors,
        rate: visitors > 0 ? num(t.v) / visitors : null,
      },
      series,
      files: this.propCounts('download', 'file', range, filters),
      fileOs: this.propCounts('download', 'os', range, filters),
      packages: this.propCounts('download', 'package', range, filters),
      visitorOs,
      via: this.propCounts('download', 'via', range, filters),
      lessons: this.propCounts('lesson-download', 'file', range, filters),
      pages,
      outbound: this.propCounts('outbound', 'target', range, filters),
      outboundUrls: this.propCounts('outbound', 'url', range, filters),
    };
  }
}
