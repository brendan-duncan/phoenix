// Storage, dashboard queries, retention, auth and the location database.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { Readable } from 'node:stream';
import { afterEach, beforeEach, describe, it } from 'node:test';
import { AuthGate, hashPassword, parseBasicAuth, verifyPassword } from '../collector/lib/auth.mjs';
import { GeoLookup, importGeoCsv, parseIPv4, parseIPv6, splitCsv } from '../collector/lib/geo.mjs';
import { classifyPath } from '../collector/lib/ingest.mjs';
import { Queries } from '../collector/lib/queries.mjs';
import { Store, utcDay } from '../collector/lib/store.mjs';

const DAY = 86400000;
const T0 = Date.UTC(2026, 9, 10, 10, 0, 0);

function row(r) {
  const p = r.path ?? '/';
  return {
    ts: r.ts,
    name: r.name ?? 'pageview',
    path: p,
    query: '',
    kind: classifyPath(p),
    sid: r.sid,
    visitor: r.visitor,
    refHost: r.refHost ?? '',
    referrer: '',
    country: r.country ?? 'US',
    browser: 'Chrome',
    os: r.os ?? 'Windows',
    device: 'desktop',
    screen: '1920x1080',
    lang: 'en-us',
    props: r.props ?? null,
  };
}

const keys = (rows) => rows.map((r) => r.key);

describe('Store + Queries', () => {
  let store;
  let q;

  beforeEach(() => {
    store = new Store(':memory:');
    q = new Queries(store);
    store.insertEvents([
      // Session a: two pages over 90 s, arrived from github, reads the timeline page.
      row({ ts: T0, path: '/', sid: 'a', visitor: 'v1', refHost: 'github.com' }),
      row({ ts: T0 + 90000, path: '/timeline', sid: 'a', visitor: 'v1' }),
      row({ ts: T0 + 60000, name: 'read-depth', path: '/timeline', sid: 'a', visitor: 'v1', props: { depth: 50 } }),
      row({ ts: T0 + 61000, name: 'section', path: '/timeline', sid: 'a', visitor: 'v1', props: { id: 'layers', n: 1, of: 3, level: 2 } }),
      row({ ts: T0 + 62000, name: 'section', path: '/timeline', sid: 'a', visitor: 'v1', props: { id: 'frames', n: 2, of: 3, level: 3 } }),
      row({ ts: T0 + 63000, name: 'time-on-page', path: '/timeline', sid: 'a', visitor: 'v1', props: { seconds: 40, depth: 60, sections: 2, of: 3 } }),
      // Session b: bounce on the install page with a download.
      row({ ts: T0 + 1000, path: '/get-started', sid: 'b', visitor: 'v2', country: 'DE', os: 'macOS' }),
      row({ ts: T0 + 2000, name: 'download', path: '/get-started', sid: 'b', visitor: 'v2', os: 'macOS',
        props: { file: 'Phoenix-osx-arm64-Setup.pkg', os: 'macos', package: 'installer', via: 'site' } }),
      row({ ts: T0 + 3000, name: 'search', path: '/get-started', sid: 'b', visitor: 'v2', props: { len: 7, words: 1, results: 4, chosen: true, rank: 2 } }),
      row({ ts: T0 + 4000, name: 'outbound', path: '/get-started', sid: 'b', visitor: 'v2',
        props: { target: 'github-releases', host: 'github.com', url: 'https://github.com/brendan-duncan/phoenix/releases' } }),
      // A day later: session c.
      row({ ts: T0 + DAY, path: '/', sid: 'c', visitor: 'v3' }),
    ]);
  });

  afterEach(() => {
    store.close();
  });

  const all = { from: T0 - DAY, to: T0 + 2 * DAY };

  it('computes totals like the overview tiles', () => {
    const t = q.totals(all, {});
    assert.equal(t.views, 4);
    assert.equal(t.visitors, 3);
    assert.equal(t.visits, 3);
    assert.equal(t.bounces, 2);
    assert.equal(t.downloads, 1);
    assert.ok(Math.abs(t.bounceRate - 2 / 3) < 1e-9);
    // Session a spans 90 s, b spans 3 s (its last event), c is a single event.
    assert.equal(t.durationSec, 93);
  });

  it('filters by dimension and by session referrer', () => {
    assert.equal(q.totals(all, { country: 'DE' }).views, 1);
    const fromGithub = q.totals(all, { referrer: 'github.com' });
    assert.equal(fromGithub.views, 2);
    assert.equal(fromGithub.visits, 1);
    assert.equal(q.totals(all, { kind: 'install' }).views, 1);
  });

  it('builds series and breakdowns', () => {
    assert.deepEqual(q.series(all, {}, 'day', 0), [
      { t: utcDay(T0), views: 3, visitors: 2 },
      { t: utcDay(T0 + DAY), views: 1, visitors: 1 },
    ]);
    assert.deepEqual(q.breakdown('path', all, {})[0], { key: '/', views: 2, visitors: 2 });
    assert.deepEqual(keys(q.breakdown('entry', all, {})).sort(), ['/', '/get-started']);
    assert.equal(q.breakdown('exit', all, {}).find((r) => r.key === '/timeline')?.views, 1);
    assert.deepEqual(keys(q.breakdown('kind', all, {})).sort(), ['guide', 'home', 'install']);
  });

  it('reports custom events, props and numeric stats', () => {
    const names = q.eventNames(all, {}).map((e) => e.name);
    for (const n of ['download', 'search', 'section', 'read-depth', 'outbound']) {
      assert.ok(names.includes(n), n);
    }
    assert.deepEqual(q.eventProps('search', all, {}).map((p) => p.key).sort(), ['chosen', 'len', 'rank', 'results', 'words']);
    const len = q.eventValues('search', 'len', all, {});
    assert.equal(len.stats.n, 1);
    assert.equal(len.stats.max, 7);
    assert.throws(() => q.eventValues('search', "x') OR 1=1 --", all, {}));
  });

  it('summarizes reading: depth, headings reached, search and tutorials', () => {
    store.insertEvents([
      row({ ts: T0 + 5000, path: '/tutorial-masks', sid: 'd', visitor: 'v4' }),
      row({ ts: T0 + 5001, name: 'tutorial-open', path: '/tutorial-masks', sid: 'd', visitor: 'v4', props: { tutorial: 'tutorial-masks', from: '/tutorials' } }),
      row({ ts: T0 + 5002, name: 'search', path: '/tutorial-masks', sid: 'd', visitor: 'v4', props: { len: 3, words: 1, results: 0, chosen: false } }),
      row({ ts: T0 + 5003, name: 'theme', path: '/tutorial-masks', sid: 'd', visitor: 'v4', props: { to: 'dark' } }),
    ]);
    const r = q.reading(all, {});
    const timeline = r.pages.find((p) => p.path === '/timeline');
    assert.equal(timeline.views, 1);
    assert.equal(timeline.d50, 1);
    assert.equal(timeline.avgSec, 40);
    assert.equal(timeline.avgDepth, 60);
    assert.deepEqual(r.sections.filter((s) => s.path === '/timeline'), [
      { path: '/timeline', id: 'layers', n: 1, level: 2, visits: 1 },
      { path: '/timeline', id: 'frames', n: 2, level: 3, visits: 1 },
    ]);
    assert.equal(r.search.count, 2);
    assert.equal(r.search.chosen, 1);
    assert.equal(r.search.zero, 1);
    assert.equal(r.search.avgRank, 2);
    assert.equal(r.search.avgLen, 5);
    // Only lengths and counts come back: no query text anywhere.
    assert.ok(!JSON.stringify(r.search).includes('"q"'));
    assert.deepEqual(r.tutorials, [{ key: 'tutorial-masks', count: 1, visitors: 1, fromIndex: 1 }]);
    assert.equal(r.themes[0].key, 'dark');
  });

  it('summarizes downloads by file, platform and visitor OS', () => {
    store.insertEvents([
      row({ ts: T0 + 6000, name: 'download', path: '/get-started', sid: 'e', visitor: 'v5',
        props: { file: 'Phoenix-win-x64-Setup.exe', os: 'windows', package: 'installer', via: 'site' } }),
      row({ ts: T0 + 6001, name: 'download', path: '/', sid: 'e', visitor: 'v5',
        props: { file: 'Phoenix-win-x64-Portable.zip', os: 'windows', package: 'portable', via: 'site' } }),
      row({ ts: T0 + 6002, name: 'lesson-download', path: '/tutorial-crowd', sid: 'e', visitor: 'v5', props: { file: 'crowd.fla' } }),
    ]);
    const d = q.downloads(all, {}, 'day', 0);
    assert.equal(d.tiles.downloads, 3);
    assert.equal(d.tiles.downloaders, 2);
    assert.equal(d.tiles.lessonFiles, 1);
    assert.equal(d.tiles.visitors, 3);
    assert.deepEqual(d.fileOs.map((r) => [r.key, r.count]), [['windows', 2], ['macos', 1]]);
    assert.deepEqual(keys(d.files).sort(), ['Phoenix-osx-arm64-Setup.pkg', 'Phoenix-win-x64-Portable.zip', 'Phoenix-win-x64-Setup.exe']);
    assert.deepEqual(d.lessons.map((r) => r.key), ['crowd.fla']);
    assert.equal(d.pages[0].key, '/get-started');
    assert.deepEqual(d.visitorOs.find((r) => r.key === 'macOS'), { key: 'macOS', fileOs: 'macos', count: 1, visitors: 1 });
    assert.deepEqual(d.series, [{ t: utcDay(T0), apps: 3, lessons: 1 }]);
    assert.deepEqual(d.outbound.map((r) => r.key), ['github-releases']);
  });

  it('rolls up, applies retention, and keeps totals past it', () => {
    const now = T0 + 5 * DAY;
    store.saveSettings({ retentionDays: 2 });
    const summary = store.applyRetention(now);
    assert.equal(summary.rolled, 2);
    assert.equal(summary.deleted, 11);
    assert.equal(store.status().events, 0);
    // Totals for the old range now come from the daily rollups.
    const t = q.totals(all, {});
    assert.equal(t.views, 4);
    assert.equal(t.downloads, 1);
    assert.deepEqual(q.series(all, {}, 'day', 0).map((p) => p.views), [3, 1]);
    assert.equal(q.breakdown('country', all, {}).find((r) => r.key === 'DE')?.views, 1);
  });

  it('combines rollups with newer raw events', () => {
    store.saveSettings({ retentionDays: 2 });
    store.applyRetention(T0 + DAY + DAY / 2 + 2 * DAY);
    store.insertEvents([row({ ts: T0 + 3 * DAY, path: '/', sid: 'd', visitor: 'v4' })]);
    assert.equal(q.totals({ from: T0 - DAY, to: T0 + 4 * DAY }, {}).views, 5);
  });

  it('reports location and version breakdowns', () => {
    store.insertEvents([{ ...row({ ts: T0 + 40, path: '/', sid: 'g', visitor: 'v7', country: 'US' }), region: 'Oregon', city: 'Portland' }]);
    assert.equal(q.breakdown('region', all, {}).find((r) => r.key === 'US|Oregon')?.views, 1);
    assert.equal(q.breakdown('city', all, {}).find((r) => r.key === 'US|Oregon|Portland')?.views, 1);
    assert.equal(q.totals(all, { region: 'US|Oregon' }).views, 1);
  });

  it('cleans up by name, path prefix and range', () => {
    assert.equal(store.cleanup({ name: 'section' }).deleted, 2);
    // LIKE wildcards in the prefix are literal.
    assert.equal(store.cleanup({ pathPrefix: '/get%' }).deleted, 0);
    assert.equal(store.cleanup({ pathPrefix: '/get-started' }).deleted, 4);
    assert.equal(store.cleanup({ from: T0 + DAY - 1000, to: T0 + DAY + 1000 }).deleted, 1);
    assert.throws(() => store.cleanup({}));
    assert.equal(store.status().events, 4);
  });

  it('persists settings, credentials and a daily salt', () => {
    store.saveSettings({ maxDbMB: 64 });
    assert.equal(store.loadSettings().maxDbMB, 64);
    const a = store.visitorHash(T0, '1.2.3.4', 'ua', 'phoenixanimate.com');
    assert.equal(store.visitorHash(T0 + 1000, '1.2.3.4', 'ua', 'phoenixanimate.com'), a);
    assert.notEqual(store.visitorHash(T0 + DAY, '1.2.3.4', 'ua', 'phoenixanimate.com'), a);
    store.setCredentials('admin', hashPassword('correct horse'));
    assert.equal(store.credentials()?.user, 'admin');
  });
});

describe('auth', () => {
  it('hashes and verifies passwords', () => {
    const h = hashPassword('correct horse battery');
    assert.equal(verifyPassword('correct horse battery', h), true);
    assert.equal(verifyPassword('wrong', h), false);
    assert.deepEqual(parseBasicAuth(`Basic ${Buffer.from('admin:a:b').toString('base64')}`), { user: 'admin', password: 'a:b' });
  });

  it('locks an IP out after repeated failures', () => {
    const gate = new AuthGate();
    const creds = { user: 'admin', hash: hashPassword('right-password') };
    const bad = `Basic ${Buffer.from('admin:nope').toString('base64')}`;
    const good = `Basic ${Buffer.from('admin:right-password').toString('base64')}`;
    assert.equal(gate.check('9.9.9.9', undefined, null), 'unset');
    assert.equal(gate.check('9.9.9.9', undefined, creds), 'missing');
    for (let i = 0; i < 10; i++) {
      assert.equal(gate.check('9.9.9.9', bad, creds), 'bad');
    }
    assert.equal(gate.check('9.9.9.9', good, creds), 'locked');
    assert.equal(gate.check('8.8.8.8', good, creds), 'ok');
  });
});

describe('geo', () => {
  it('parses addresses', () => {
    assert.equal(parseIPv4('1.2.3.4'), 0x01020304);
    assert.equal(parseIPv4('256.0.0.1'), null);
    assert.equal(parseIPv6('2001:db8::1')?.toString('hex'), '20010db8000000000000000000000001');
    assert.equal(parseIPv6('::ffff:1.2.3.4')?.toString('hex'), '00000000000000000000ffff01020304');
    assert.equal(parseIPv6('1.2.3.4'), null);
  });

  it('imports a DB-IP csv and looks up countries', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'phoenix-geo-'));
    const file = path.join(dir, 'geo.db');
    const csv = ['1.0.0.0,1.0.0.255,AU', '8.8.8.0,8.8.8.255,US', '2001:db8::,2001:db8:ffff:ffff:ffff:ffff:ffff:ffff,NL'].join('\n');
    assert.deepEqual(await importGeoCsv(Readable.from([csv]), file, false), { v4: 2, v6: 1, locations: 3, cities: false });
    const geo = new GeoLookup(file);
    assert.equal(geo.country('8.8.8.8'), 'US');
    assert.equal(geo.country('::ffff:1.0.0.7'), 'AU');
    assert.equal(geo.country('2001:db8::42'), 'NL');
    assert.equal(geo.country('9.9.9.9'), '');
    geo.close();
    fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
  });

  it('imports the city file, merging adjacent ranges and honoring quotes', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'phoenix-geo-'));
    const file = path.join(dir, 'geo.db');
    const csv = [
      '0.0.0.0,0.255.255.255,ZZ,ZZ,,,0,0',
      '8.8.4.0,8.8.7.255,NA,US,California,"Mountain View",37.4,-122.1',
      '8.8.8.0,8.8.8.255,NA,US,California,"Mountain View",37.4,-122.1',
      '9.0.0.0,9.0.0.255,NA,US,"District of Columbia","Washington, D.C.",38.9,-77.0',
      '2001:db8::,2001:db8::ffff,EU,DE,Hesse,"Frankfurt am Main",50.1,8.7',
    ].join('\n');
    const counts = await importGeoCsv(Readable.from([csv]), file, false);
    assert.equal(counts.v4, 3);
    assert.equal(counts.v6, 1);
    assert.equal(counts.cities, true);
    const geo = new GeoLookup(file);
    assert.equal(geo.hasCities, true);
    assert.deepEqual(geo.lookup('8.8.5.1'), { country: 'US', region: 'California', city: 'Mountain View' });
    assert.deepEqual(geo.lookup('8.8.8.8'), { country: 'US', region: 'California', city: 'Mountain View' });
    assert.equal(geo.lookup('9.0.0.9').city, 'Washington, D.C.');
    assert.deepEqual(geo.lookup('0.1.2.3'), { country: '', region: '', city: '' });
    assert.equal(geo.lookup('2001:db8::1').region, 'Hesse');
    geo.close();
    fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
  });

  it('splits quoted CSV fields', () => {
    assert.deepEqual(splitCsv('a,"b, c","d ""e""",'), ['a', 'b, c', 'd "e"', '']);
  });
});
