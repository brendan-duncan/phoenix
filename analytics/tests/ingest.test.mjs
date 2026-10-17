// Beacon validation, URL helpers, rate limits, user agents and settings.
import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { CONFIG, validateSettings } from '../collector/lib/config.mjs';
import {
  IpRateLimiter,
  TokenBucket,
  classifyPath,
  clientIp,
  normalizeReferrer,
  parsePayload,
  splitUrl,
} from '../collector/lib/ingest.mjs';
import { isBotUserAgent, parseUserAgent } from '../collector/lib/ua.mjs';

const NOW = Date.UTC(2026, 9, 16, 12, 0, 0);

function beacon(overrides = {}) {
  return {
    v: 1,
    h: 'phoenixanimate.com',
    sid: 'abc123def456',
    sc: '1920x1080',
    lg: 'en-US',
    tp: 0,
    e: [{ n: 'pageview', u: '/get-started.html?x=1#windows', a: 0, r: 'https://www.google.com/search?q=phoenix' }],
    ...overrides,
  };
}

function ok(r) {
  if ('error' in r) {
    throw new Error(r.error);
  }
  return r;
}

describe('config', () => {
  it('defaults to its own port and the phoenixanimate.com hosts', () => {
    assert.equal(CONFIG.port, 8791);
    assert.deepEqual(CONFIG.allowedHosts, ['phoenixanimate.com', 'www.phoenixanimate.com']);
  });
});

describe('parsePayload', () => {
  it('accepts a well-formed beacon and normalizes the url', () => {
    const r = ok(parsePayload(beacon(), NOW));
    assert.equal(r.ctx.sid, 'abc123def456');
    assert.equal(r.ctx.lang, 'en-us');
    assert.equal(r.events.length, 1);
    assert.deepEqual(r.events[0], {
      ts: NOW,
      name: 'pageview',
      path: '/get-started',
      query: 'x=1',
      kind: 'install',
      refHost: 'google.com',
      referrer: 'https://www.google.com/search',
      props: null,
    });
  });

  it('accepts the www host too', () => {
    assert.equal(ok(parsePayload(beacon({ h: 'www.phoenixanimate.com' }), NOW)).ctx.host, 'www.phoenixanimate.com');
    assert.equal(ok(parsePayload(beacon({ h: 'PhoenixAnimate.com' }), NOW)).ctx.host, 'phoenixanimate.com');
  });

  it('rejects other hosts, bad sessions and empty batches', () => {
    for (const h of ['evil.example', 'localhost', '', '127.0.0.1', 'phoenixanimate.com.evil.example', 'taosengine.com']) {
      assert.deepEqual(parsePayload(beacon({ h }), NOW), { error: 'host not allowed' }, h);
    }
    assert.deepEqual(parsePayload(beacon({ sid: 'x' }), NOW), { error: 'bad sid' });
    assert.deepEqual(parsePayload(beacon({ e: [] }), NOW), { error: 'no events' });
    assert.deepEqual(parsePayload(beacon({ v: 2 }), NOW), { error: 'bad version' });
    assert.deepEqual(parsePayload('nope', NOW), { error: 'not an object' });
  });

  it('skips malformed events, clamps age and cleans props', () => {
    const r = ok(parsePayload(beacon({
      e: [
        { n: 'bad name!', u: '/' },
        { n: 'download', u: '/get-started.html', a: 99999999,
          d: { file: 'Phoenix-win-x64-Setup.exe', os: 'windows', nested: { x: 1 }, 'bad key': 1, ok: true } },
      ],
    }), NOW));
    assert.equal(r.events.length, 1);
    const e = r.events[0];
    assert.equal(e.ts, NOW - 10 * 60 * 1000);
    assert.equal(e.path, '/get-started');
    assert.equal(e.kind, 'install');
    assert.equal(e.refHost, '');
    assert.deepEqual(e.props, { file: 'Phoenix-win-x64-Setup.exe', os: 'windows', ok: true });
  });

  it('keeps at most 16 properties per event', () => {
    const d = Object.fromEntries(Array.from({ length: 40 }, (_, i) => [`p${i}`, i]));
    const r = ok(parsePayload(beacon({ e: [{ n: 'x', u: '/', d }] }), NOW));
    assert.equal(Object.keys(r.events[0].props).length, 16);
  });

  it('accepts client hints and rejects junk in them', () => {
    const r = ok(parsePayload(beacon({ ch: { b: 'Google Chrome', bv: '140.0.7339.80', pv: '15.0.0' } }), NOW));
    assert.deepEqual(r.ctx.hints, { brand: 'Google Chrome', version: '140.0.7339.80', platformVersion: '15.0.0' });
    const bad = ok(parsePayload(beacon({ ch: { b: '<script>', bv: 'x'.repeat(100) } }), NOW));
    assert.equal(bad.ctx.hints, null);
  });

  it('caps batch size', () => {
    const many = Array.from({ length: 100 }, () => ({ n: 'x', u: '/' }));
    assert.equal(ok(parsePayload(beacon({ e: many }), NOW)).events.length, 30);
  });
});

describe('url helpers', () => {
  it('classifies guide sections', () => {
    assert.equal(classifyPath('/'), 'home');
    assert.equal(classifyPath('/get-started'), 'install');
    assert.equal(classifyPath('/tutorials'), 'tutorials');
    assert.equal(classifyPath('/first-animation'), 'tutorial');
    assert.equal(classifyPath('/tutorial-masks'), 'tutorial');
    assert.equal(classifyPath('/privacy'), 'about');
    assert.equal(classifyPath('/timeline'), 'guide');
  });

  it('splits and normalizes paths', () => {
    assert.deepEqual(splitUrl('//index.html?a=b#h'), { path: '/', query: 'a=b' });
    assert.deepEqual(splitUrl('/tutorial-masks.html'), { path: '/tutorial-masks', query: '' });
    assert.deepEqual(splitUrl(undefined), { path: '/', query: '' });
  });

  it('drops same-site referrers', () => {
    assert.deepEqual(normalizeReferrer('https://www.phoenixanimate.com/timeline.html', 'phoenixanimate.com'), { host: '', url: '' });
    assert.deepEqual(normalizeReferrer('https://phoenixanimate.com/', 'www.phoenixanimate.com'), { host: '', url: '' });
    assert.deepEqual(normalizeReferrer('not a url', 'phoenixanimate.com'), { host: '', url: '' });
    assert.deepEqual(normalizeReferrer('https://github.com/x/y?z', 'phoenixanimate.com'), { host: 'github.com', url: 'https://github.com/x/y' });
  });
});

describe('clientIp', () => {
  const req = (peer, xff) => ({ socket: { remoteAddress: peer }, headers: xff ? { 'x-forwarded-for': xff } : {} });

  it('trusts only the last X-Forwarded-For hop, and only from the local proxy', () => {
    assert.equal(clientIp(req('127.0.0.1', '6.6.6.6, 203.0.113.9')), '203.0.113.9');
    assert.equal(clientIp(req('198.51.100.1', '6.6.6.6')), '198.51.100.1');
    assert.equal(clientIp(req('::ffff:198.51.100.2')), '198.51.100.2');
  });
});

describe('rate limiting', () => {
  it('token bucket refills over time', () => {
    const t = 1000000;
    const b = new TokenBucket(2, 1, t);
    assert.equal(b.take(1, t), true);
    assert.equal(b.take(1, t), true);
    assert.equal(b.take(1, t), false);
    assert.equal(b.take(1, t + 1000), true);
  });

  it('puts repeat offenders in the penalty box', () => {
    const lim = new IpRateLimiter(10, { penaltyStrikes: 3, penaltyMs: 60000, maxIps: 100 });
    const t = 5000000;
    assert.equal(lim.check('1.2.3.4', 10, t), 'ok');
    assert.equal(lim.check('1.2.3.4', 1, t), 'rate');
    assert.equal(lim.check('1.2.3.4', 1, t), 'rate');
    assert.equal(lim.check('1.2.3.4', 1, t), 'rate');
    assert.equal(lim.check('1.2.3.4', 1, t + 30000), 'blocked');
    assert.equal(lim.blockedCount(t + 30000), 1);
    assert.equal(lim.check('5.6.7.8', 1, t), 'ok');
    assert.equal(lim.check('1.2.3.4', 1, t + 61000), 'ok');
  });

  it('bounds memory under an IP-rotating flood', () => {
    const lim = new IpRateLimiter(10, { maxIps: 50 });
    for (let i = 0; i < 500; i++) {
      lim.check(`10.0.${i >> 8}.${i & 255}`, 1, 1000);
    }
    assert.ok(lim.ips.size <= 50);
  });
});

describe('user agents', () => {
  const WIN_CHROME = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36';

  it('parses common browsers with versions', () => {
    assert.deepEqual(parseUserAgent(WIN_CHROME),
      { browser: 'Chrome', browserVersion: 'Chrome 140', os: 'Windows', osVersion: 'Windows 10/11', device: 'desktop' });
    assert.equal(parseUserAgent(`${WIN_CHROME} Edg/140.0.0.0`).browserVersion, 'Edge 140');
    assert.deepEqual(parseUserAgent('Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1'),
      { browser: 'Safari', browserVersion: 'Safari 18', os: 'iOS', osVersion: 'iOS 18', device: 'mobile' });
    assert.deepEqual(parseUserAgent('Mozilla/5.0 (X11; Linux x86_64; rv:130.0) Gecko/20100101 Firefox/130.0'),
      { browser: 'Firefox', browserVersion: 'Firefox 130', os: 'Linux', osVersion: 'Linux', device: 'desktop' });
    const tablet = parseUserAgent('Mozilla/5.0 (Linux; Android 14; SM-X710) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0 Safari/537.36');
    assert.equal(tablet.device, 'tablet');
    assert.equal(tablet.osVersion, 'Android 14');
    assert.equal(parseUserAgent('Mozilla/5.0 (Windows NT 6.1; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/109.0 Safari/537.36').osVersion,
      'Windows 7');
  });

  it('prefers client hints for frozen UA fields', () => {
    assert.equal(parseUserAgent(WIN_CHROME, 0, { brand: 'Google Chrome', version: '140.0.7339.80', platformVersion: '15.0.0' }).osVersion,
      'Windows 11');
    assert.equal(parseUserAgent(WIN_CHROME, 0, { platformVersion: '10.0.0' }).osVersion, 'Windows 10');
    const mac = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36';
    assert.equal(parseUserAgent(mac).osVersion, 'macOS');
    assert.equal(parseUserAgent(mac, 0, { platformVersion: '15.1.0' }).osVersion, 'macOS 15');
    assert.equal(parseUserAgent(WIN_CHROME, 0, { brand: 'Brave', version: '1.80' }).browserVersion, 'Brave 1');
  });

  it('spots iPadOS desktop mode from touch points', () => {
    const mac = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15';
    assert.equal(parseUserAgent(mac, 0).os, 'macOS');
    assert.equal(parseUserAgent(mac, 0).device, 'desktop');
    assert.equal(parseUserAgent(mac, 5).os, 'iPadOS');
    assert.equal(parseUserAgent(mac, 5).device, 'tablet');
  });

  it('flags bots and automation', () => {
    assert.equal(isBotUserAgent('Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)'), true);
    assert.equal(isBotUserAgent('Mozilla/5.0 (Windows NT 10.0) AppleWebKit/537.36 (KHTML, like Gecko) HeadlessChrome/140.0 Safari/537.36'), true);
    assert.equal(isBotUserAgent('curl/8.5.0'), true);
    assert.equal(isBotUserAgent(''), true);
    assert.equal(isBotUserAgent(WIN_CHROME), false);
  });
});

describe('validateSettings', () => {
  it('coerces and bounds settings', () => {
    assert.deepEqual(validateSettings({ retentionDays: '30', ingestPaused: 'true' }), { ok: true, value: { retentionDays: 30, ingestPaused: true } });
    assert.equal(validateSettings({ retentionDays: 0 }).ok, false);
    assert.equal(validateSettings({ perIpPerMinute: 1.5 }).ok, false);
    assert.equal(validateSettings({ bogus: 1 }).ok, false);
  });
});
