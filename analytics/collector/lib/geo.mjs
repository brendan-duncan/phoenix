// IP → location from DB-IP's free "IP to City Lite" database (CC BY 4.0,
// https://db-ip.com); the "IP to Country Lite" file works too (country only).
// Kept in its own SQLite file (geo.db) and queried per lookup, so it costs
// disk, not RAM. IPv4 ranges key on a 32-bit integer; IPv6 ranges on 16-byte
// big-endian blobs, which SQLite compares bytewise. Each distinct
// (country, region, city) is stored once in `locs`, and adjacent ranges with
// the same location are merged at import.

import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import zlib from 'node:zlib';
import { Readable } from 'node:stream';
import { DatabaseSync } from 'node:sqlite';

/** @returns {number | null} */
export function parseIPv4(ip) {
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(ip);
  if (!m) {
    return null;
  }
  let n = 0;
  for (let i = 1; i <= 4; i++) {
    const o = Number(m[i]);
    if (o > 255) {
      return null;
    }
    n = n * 256 + o;
  }
  return n;
}

/** @returns {Buffer | null} 16 bytes, big-endian */
export function parseIPv6(ip) {
  if (!ip.includes(':')) {
    return null;
  }
  let s = ip.split('%')[0];
  // Embedded IPv4 tail (e.g. ::ffff:1.2.3.4).
  const v4 = /(\d+\.\d+\.\d+\.\d+)$/.exec(s);
  if (v4) {
    const n = parseIPv4(v4[1]);
    if (n === null) {
      return null;
    }
    s = `${s.slice(0, -v4[1].length)}${(n >>> 16).toString(16)}:${(n & 0xffff).toString(16)}`;
  }
  const halves = s.split('::');
  if (halves.length > 2) {
    return null;
  }
  const head = halves[0] ? halves[0].split(':') : [];
  const tail = halves.length === 2 && halves[1] ? halves[1].split(':') : [];
  const fill = halves.length === 2 ? 8 - head.length - tail.length : 0;
  const groups = [...head, ...Array(fill).fill('0'), ...tail];
  if (groups.length !== 8) {
    return null;
  }
  const buf = Buffer.alloc(16);
  for (let i = 0; i < 8; i++) {
    if (!/^[0-9a-f]{1,4}$/i.test(groups[i])) {
      return null;
    }
    buf.writeUInt16BE(Number.parseInt(groups[i], 16), i * 2);
  }
  return buf;
}

/** Split one CSV line, honoring double-quoted fields ("a, b" and "" escapes). */
export function splitCsv(line) {
  const out = [];
  let cur = '';
  let quoted = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (quoted) {
      if (c === '"') {
        if (line[i + 1] === '"') {
          cur += '"';
          i++;
        } else {
          quoted = false;
        }
      } else {
        cur += c;
      }
    } else if (c === '"') {
      quoted = true;
    } else if (c === ',') {
      out.push(cur);
      cur = '';
    } else {
      cur += c;
    }
  }
  out.push(cur);
  return out;
}

const NO_LOCATION = Object.freeze({ country: '', region: '', city: '' });

/** Big-endian 16-byte increment (for "is this range adjacent to the last one"). */
function nextIPv6(buf) {
  const b = Buffer.from(buf);
  for (let i = 15; i >= 0; i--) {
    if (b[i] === 255) {
      b[i] = 0;
    } else {
      b[i]++;
      break;
    }
  }
  return b;
}

export class GeoLookup {
  /** @param {string} file path to geo.db; a missing file means every lookup is empty. */
  constructor(file) {
    this.file = file;
    this.db = null;
    this.hasCities = false;
    this.open();
  }

  open() {
    this.close();
    if (!fs.existsSync(this.file)) {
      return;
    }
    this.db = new DatabaseSync(this.file, { readOnly: true });
    this.q4 = this.db.prepare(`SELECT r.end, l.cc, l.region, l.city FROM ip4 r JOIN locs l ON l.id = r.loc
      WHERE r.start <= ? ORDER BY r.start DESC LIMIT 1`);
    this.q6 = this.db.prepare(`SELECT r.end, l.cc, l.region, l.city FROM ip6 r JOIN locs l ON l.id = r.loc
      WHERE r.start <= ? ORDER BY r.start DESC LIMIT 1`);
    const meta = this.db.prepare("SELECT value FROM meta WHERE key = 'cities'").get();
    this.hasCities = meta?.value === '1';
  }

  close() {
    if (this.db) {
      this.db.close();
      this.db = null;
    }
  }

  get available() {
    return this.db !== null;
  }

  /** @returns {{ country: string, region: string, city: string }} empty strings when unknown */
  lookup(ip) {
    if (!this.db || !ip) {
      return NO_LOCATION;
    }
    const mapped = /^::ffff:(\d+\.\d+\.\d+\.\d+)$/i.exec(ip);
    const n = parseIPv4(mapped ? mapped[1] : ip);
    let row = null;
    if (n !== null) {
      row = this.q4.get(n);
      if (row && n > Number(row.end)) {
        row = null;
      }
    } else {
      const b = parseIPv6(ip);
      if (b) {
        row = this.q6.get(b);
        if (row && Buffer.compare(b, Buffer.from(row.end)) > 0) {
          row = null;
        }
      }
    }
    if (!row) {
      return NO_LOCATION;
    }
    return { country: String(row.cc), region: String(row.region), city: String(row.city) };
  }

  /** @returns {string} ISO country code, or '' */
  country(ip) {
    return this.lookup(ip).country;
  }
}

/**
 * Build geo.db from a DB-IP CSV, optionally gzipped. Accepts the city file
 * (`start,end,continent,country,region,city,lat,lon`) or the country file
 * (`start,end,country`). Writes a temp file and renames it so a running
 * collector never sees a half-built database.
 * @param {Readable} input
 * @param {string} outFile
 * @param {boolean} gzipped
 */
export async function importGeoCsv(input, outFile, gzipped) {
  const tmp = `${outFile}.tmp`;
  fs.rmSync(tmp, { force: true });
  fs.mkdirSync(path.dirname(outFile), { recursive: true });
  const db = new DatabaseSync(tmp);
  db.exec(`
    PRAGMA journal_mode = OFF;
    PRAGMA synchronous = OFF;
    CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
    CREATE TABLE locs (id INTEGER PRIMARY KEY, cc TEXT NOT NULL, region TEXT NOT NULL, city TEXT NOT NULL);
    CREATE TABLE ip4 (start INTEGER PRIMARY KEY, end INTEGER NOT NULL, loc INTEGER NOT NULL);
    CREATE TABLE ip6 (start BLOB PRIMARY KEY, end BLOB NOT NULL, loc INTEGER NOT NULL) WITHOUT ROWID;
  `);
  const insLoc = db.prepare('INSERT INTO locs VALUES (?, ?, ?, ?)');
  const ins4 = db.prepare('INSERT OR REPLACE INTO ip4 VALUES (?, ?, ?)');
  const ins6 = db.prepare('INSERT OR REPLACE INTO ip6 VALUES (?, ?, ?)');
  /** @type {Map<string, number>} */
  const locIds = new Map();
  const locId = (cc, region, city) => {
    const key = `${cc}\u0000${region}\u0000${city}`;
    let id = locIds.get(key);
    if (id === undefined) {
      id = locIds.size + 1;
      locIds.set(key, id);
      insLoc.run(id, cc, region, city);
    }
    return id;
  };

  // One pending (unwritten) range per family, extended while the next range
  // is adjacent and maps to the same location.
  let p4 = null;
  let p6 = null;
  let v4 = 0;
  let v6 = 0;
  let cities = false;
  const flush4 = () => {
    if (p4) {
      ins4.run(p4.start, p4.end, p4.loc);
      v4++;
      p4 = null;
    }
  };
  const flush6 = () => {
    if (p6) {
      ins6.run(p6.start, p6.end, p6.loc);
      v6++;
      p6 = null;
    }
  };

  const stream = gzipped ? input.pipe(zlib.createGunzip()) : input;
  const lines = readline.createInterface({ input: stream, crlfDelay: Infinity });
  let batch = 0;
  db.exec('BEGIN');
  for await (const line of lines) {
    if (!line) {
      continue;
    }
    const f = splitCsv(line);
    let cc;
    let region = '';
    let city = '';
    if (f.length >= 6) {
      cc = f[3].trim();
      region = f[4].trim().slice(0, 80);
      city = f[5].trim().slice(0, 80);
      cities = true;
    } else if (f.length >= 3) {
      cc = f[2].trim();
    } else {
      continue;
    }
    if (!/^[A-Z]{2}$/.test(cc) || cc === 'ZZ') {
      cc = '';
      region = '';
      city = '';
    }
    const loc = locId(cc, region, city);
    const s4 = parseIPv4(f[0].trim());
    if (s4 !== null) {
      const e4 = parseIPv4(f[1].trim());
      if (e4 === null) {
        continue;
      }
      if (p4 && p4.loc === loc && p4.end + 1 === s4) {
        p4.end = e4;
      } else {
        flush4();
        p4 = { start: s4, end: e4, loc };
      }
    } else {
      const s6 = parseIPv6(f[0].trim());
      const e6 = parseIPv6(f[1].trim());
      if (!s6 || !e6) {
        continue;
      }
      if (p6 && p6.loc === loc && Buffer.compare(nextIPv6(p6.end), s6) === 0) {
        p6.end = e6;
      } else {
        flush6();
        p6 = { start: s6, end: e6, loc };
      }
    }
    if (++batch >= 200000) {
      db.exec('COMMIT');
      db.exec('BEGIN');
      batch = 0;
    }
  }
  flush4();
  flush6();
  db.prepare('INSERT INTO meta VALUES (?, ?)').run('cities', cities ? '1' : '0');
  db.exec('COMMIT');
  db.close();
  if (v4 === 0) {
    fs.rmSync(tmp, { force: true });
    throw new Error('no IPv4 ranges found; is this a DB-IP country or city CSV?');
  }
  fs.renameSync(tmp, outFile);
  return { v4, v6, locations: locIds.size, cities };
}

/**
 * Download this month's DB-IP Lite CSV (falls back to last month early in a
 * month) and build geo.db from it. `kind` is 'city' (default) or 'country'.
 */
export async function downloadGeo(outFile, kind = 'city') {
  const now = new Date();
  const months = [0, 1].map((back) => {
    const d = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth() - back, 1));
    return `${d.getUTCFullYear()}-${String(d.getUTCMonth() + 1).padStart(2, '0')}`;
  });
  for (const m of months) {
    const url = `https://download.db-ip.com/free/dbip-${kind}-lite-${m}.csv.gz`;
    const res = await fetch(url);
    if (res.ok && res.body) {
      const counts = await importGeoCsv(Readable.fromWeb(res.body), outFile, true);
      return { url, ...counts };
    }
  }
  throw new Error(`could not download DB-IP ${kind} lite for ${months.join(' or ')}`);
}
