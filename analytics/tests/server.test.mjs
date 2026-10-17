// End-to-end: spawn the real collector on a temp data dir, post beacons the way
// the guide's client does (through a loopback "proxy" with X-Forwarded-For), and
// read them back through the password-protected dashboard API.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { after, before, describe, it } from 'node:test';
import { fileURLToPath } from 'node:url';
import { hashPassword } from '../collector/lib/auth.mjs';
import { Store } from '../collector/lib/store.mjs';

const SERVER = fileURLToPath(new URL('../collector/server.mjs', import.meta.url));
const PORT = 18791 + Math.floor(Math.random() * 1000);
const BASE = `http://127.0.0.1:${PORT}`;
const AUTH = `Basic ${Buffer.from('admin:test-password-123').toString('base64')}`;
const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36';

let dir = '';
let child;

function beacon(sid, events, host = 'phoenixanimate.com') {
  return JSON.stringify({ v: 1, h: host, sid, sc: '1920x1080', lg: 'en-US', tp: 0, e: events });
}

function collect(body, headers = {}) {
  return fetch(`${BASE}/collect`, {
    method: 'POST',
    body,
    headers: { 'Content-Type': 'text/plain', 'User-Agent': UA, Origin: 'https://phoenixanimate.com', 'X-Forwarded-For': '203.0.113.7', ...headers },
  });
}

function dash(route, init = {}) {
  return fetch(`${BASE}/dashboard/${route}`, { ...init, headers: { Authorization: AUTH, 'x-phoenix-dash': '1', ...(init.headers ?? {}) } });
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const span = () => {
  const now = Date.now();
  return `from=${now - 3600000}&to=${now + 60000}`;
};

before(async () => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), 'phoenix-analytics-'));
  const store = new Store(path.join(dir, 'analytics.db'));
  store.setCredentials('admin', hashPassword('test-password-123'));
  store.saveSettings({ perIpPerMinute: 20 });
  store.close();
  child = spawn(process.execPath, ['--disable-warning=ExperimentalWarning', SERVER], {
    env: { ...process.env, ANALYTICS_DATA_DIR: dir, ANALYTICS_PORT: String(PORT) },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('collector did not start')), 10000);
    child.stdout.on('data', (d) => {
      if (d.toString().includes('listening')) {
        clearTimeout(timer);
        resolve();
      }
    });
    child.on('exit', (code) => reject(new Error(`collector exited with ${code}`)));
  });
});

after(async () => {
  if (child && child.exitCode === null) {
    const exited = new Promise((resolve) => child.once('exit', resolve));
    child.kill();
    await exited;
  }
  // Windows can hold the SQLite file briefly after the process exits.
  fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
});

describe('collector server', () => {
  it('answers health checks', async () => {
    assert.deepEqual(await (await fetch(`${BASE}/health`)).json(), { ok: true });
  });

  it('stores beacons and serves them to the dashboard', async () => {
    const r1 = await collect(beacon('sessionone1', [
      { n: 'pageview', u: '/get-started.html', r: 'https://github.com/x' },
      { n: 'download', u: '/get-started.html', d: { file: 'Phoenix-win-x64-Setup.exe', os: 'windows', package: 'installer', via: 'site' } },
      { n: 'read-depth', u: '/get-started.html', d: { depth: 50 } },
    ]));
    assert.equal(r1.status, 204);
    await collect(beacon('sessiontwo2', [{ n: 'pageview', u: '/tutorial-masks.html' }], 'www.phoenixanimate.com'),
      { 'X-Forwarded-For': '198.51.100.4', Origin: 'https://www.phoenixanimate.com' });
    await sleep(1300);

    const summary = await (await dash(`api/summary?${span()}`)).json();
    assert.equal(summary.current.views, 2);
    assert.equal(summary.current.visitors, 2);
    assert.equal(summary.current.visits, 2);
    assert.equal(summary.current.downloads, 1);
    const refs = await (await dash(`api/breakdown?dim=referrer&${span()}`)).json();
    assert.ok(refs.map((r) => r.key).includes('github.com'));
    const kinds = await (await dash(`api/breakdown?dim=kind&${span()}`)).json();
    assert.deepEqual(kinds.map((r) => r.key).sort(), ['install', 'tutorial']);
    const dl = await (await dash(`api/downloads?${span()}`)).json();
    assert.equal(dl.tiles.downloads, 1);
    assert.deepEqual(dl.files.map((r) => r.key), ['Phoenix-win-x64-Setup.exe']);
    const reading = await (await dash(`api/reading?${span()}`)).json();
    assert.equal(reading.pages.find((p) => p.path === '/get-started')?.d50, 1);
    const rt = await (await dash('api/realtime')).json();
    assert.equal(rt.views, 2);
    assert.equal((await dash(`api/engine?${span()}`)).status, 404);
  });

  it('drops bots, foreign origins and hosts, and invalid bodies, and counts them', async () => {
    assert.equal((await collect(beacon('botsession1', [{ n: 'pageview', u: '/' }]), { 'User-Agent': 'curl/8.5.0' })).status, 204);
    assert.equal((await collect(beacon('evilsession', [{ n: 'pageview', u: '/' }]), { Origin: 'https://evil.example' })).status, 204);
    assert.equal((await collect(beacon('localsessn1', [{ n: 'pageview', u: '/' }], 'localhost'), { Origin: 'http://localhost:8000' })).status, 204);
    assert.equal((await collect('{not json')).status, 204);
    assert.equal((await collect('x'.repeat(20000))).status, 413);
    await sleep(1300);
    const st = await (await dash('api/status')).json();
    assert.equal(st.sinceStart.bot, 1);
    assert.ok(st.sinceStart.invalid >= 3);
    assert.equal(st.sinceStart['too-large'], 1);
    const summary = await (await dash(`api/summary?${span()}`)).json();
    assert.equal(summary.current.views, 2);
  });

  it('rate-limits a single hammering IP without affecting others', async () => {
    const ip = { 'X-Forwarded-For': '192.0.2.99' };
    const events = Array.from({ length: 10 }, () => ({ n: 'x-flood', u: '/' }));
    for (let i = 0; i < 5; i++) {
      await collect(beacon('floodsessn1', events), ip);
    }
    await collect(beacon('calmsession', [{ n: 'pageview', u: '/calm' }]), { 'X-Forwarded-For': '192.0.2.100' });
    await sleep(1300);
    const st = await (await dash('api/status')).json();
    // 20 events/min per IP: two batches of 10 get in, the other three are refused.
    assert.equal(st.sinceStart['ip-rate'], 30);
    const pages = await (await dash(`api/breakdown?dim=path&${span()}`)).json();
    assert.ok(pages.map((r) => r.key).includes('/calm'));
  });

  it('protects the dashboard', async () => {
    assert.equal((await fetch(`${BASE}/dashboard/`)).status, 401);
    const wrong = await fetch(`${BASE}/dashboard/`, { headers: { Authorization: `Basic ${Buffer.from('admin:nope').toString('base64')}` } });
    assert.equal(wrong.status, 401);
    const page = await dash('');
    assert.equal(page.status, 200);
    assert.ok((await page.text()).includes('Phoenix'));
    assert.ok(page.headers.get('content-security-policy').includes("script-src 'self'"));
    // Mutations need the anti-CSRF header.
    const noHeader = await fetch(`${BASE}/dashboard/api/settings`, { method: 'POST', body: '{}', headers: { Authorization: AUTH } });
    assert.equal(noHeader.status, 403);
    const bad = await dash('api/settings', { method: 'POST', body: JSON.stringify({ retentionDays: 0 }) });
    assert.equal(bad.status, 400);
    const ok = await dash('api/settings', { method: 'POST', body: JSON.stringify({ retentionDays: 30 }) });
    assert.equal((await ok.json()).settings.retentionDays, 30);
  });

  it('exports CSV', async () => {
    const res = await dash(`api/export?${span()}`);
    assert.equal(res.status, 200);
    assert.match(res.headers.get('content-disposition'), /phoenix-events-/);
    const text = await res.text();
    assert.ok(text.startsWith('ts,name,path'));
    assert.ok(text.includes('Phoenix-win-x64-Setup.exe'));
  });

  it('pauses ingestion and cleans up from the admin API', async () => {
    await dash('api/settings', { method: 'POST', body: JSON.stringify({ ingestPaused: true }) });
    await collect(beacon('pausedsess1', [{ n: 'pageview', u: '/paused' }]), { 'X-Forwarded-For': '192.0.2.200' });
    await dash('api/settings', { method: 'POST', body: JSON.stringify({ ingestPaused: false }) });
    const cleaned = await (await dash('api/cleanup', { method: 'POST', body: JSON.stringify({ name: 'x-flood' }) })).json();
    assert.equal(cleaned.deleted, 20);
    const st = await (await dash('api/status')).json();
    assert.equal(st.sinceStart.paused, 1);
    assert.ok(!st.eventNames.map((e) => e.name).includes('x-flood'));
  });
});
