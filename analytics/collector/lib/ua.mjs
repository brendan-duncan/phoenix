// Minimal User-Agent parsing: browser family + major version, OS family +
// version, device class, and a bot check. Order matters: Chromium-based
// browsers all say "Chrome", Chrome says "Safari", and iPadOS/Quest identify as
// other platforms.
//
// UA strings are increasingly frozen (Chrome reports Windows 10 on Windows 11
// and macOS 10.15.7 on every Mac), so Chromium clients also send User-Agent
// Client Hints (`hints`), which take precedence when present.

const BOT_RE = /bot\b|bot\/|crawl|spider|slurp|headless|phantom|puppeteer|playwright|selenium|webdriver|lighthouse|pagespeed|gtmetrix|curl\/|wget|python|httpclient|java\/|go-http|axios|node-fetch|undici|okhttp|scrapy|facebookexternalhit|embedly|preview|monitor|uptime|pingdom|statuscake|datadog/i;

// [pattern, family, version regex]
const BROWSERS = [
  [/OculusBrowser\//, 'Quest Browser', /OculusBrowser\/(\d+)/],
  [/Edg(e|A|iOS)?\//, 'Edge', /Edg(?:e|A|iOS)?\/(\d+)/],
  [/OPR\/|Opera/, 'Opera', /OPR\/(\d+)/],
  [/SamsungBrowser\//, 'Samsung Internet', /SamsungBrowser\/(\d+)/],
  [/YaBrowser\//, 'Yandex', /YaBrowser\/(\d+)/],
  [/Vivaldi\//, 'Vivaldi', /Vivaldi\/(\d+)/],
  [/Firefox\/|FxiOS\//, 'Firefox', /(?:Firefox|FxiOS)\/(\d+)/],
  [/CriOS\//, 'Chrome', /CriOS\/(\d+)/],
  [/Chrome\/|Chromium\//, 'Chrome', /(?:Chrome|Chromium)\/(\d+)/],
  [/Version\/[\d.]+.*Safari\//, 'Safari', /Version\/(\d+)/],
];

const SYSTEMS = [
  [/Quest/, 'Quest'],
  [/Windows NT|Windows Phone/, 'Windows'],
  [/iPad/, 'iPadOS'],
  [/iPhone|iPod/, 'iOS'],
  [/CrOS/, 'ChromeOS'],
  [/Android/, 'Android'],
  [/Mac OS X|Macintosh/, 'macOS'],
  [/Linux|X11/, 'Linux'],
];

// Client-hint brand → our family name.
const HINT_BRANDS = [
  [/Microsoft Edge/i, 'Edge'],
  [/Opera/i, 'Opera'],
  [/Samsung/i, 'Samsung Internet'],
  [/Yandex/i, 'Yandex'],
  [/Vivaldi/i, 'Vivaldi'],
  [/Google Chrome/i, 'Chrome'],
  [/Brave/i, 'Brave'],
];

/** @param {string | undefined} ua */
export function isBotUserAgent(ua) {
  return !ua || ua.length < 20 || BOT_RE.test(ua);
}

function osVersionFromUa(os, s) {
  let m;
  switch (os) {
    case 'Windows':
      m = /Windows NT (\d+\.\d+)/.exec(s);
      if (!m) {
        return '';
      }
      // NT 10.0 is both Windows 10 and 11; only client hints can tell them apart.
      return { '10.0': '10/11', '6.3': '8.1', '6.2': '8', '6.1': '7' }[m[1]] ?? '';
    case 'iOS':
    case 'iPadOS':
      m = /OS (\d+)[_.]\d/.exec(s);
      return m ? m[1] : '';
    case 'Android':
      m = /Android (\d+)/.exec(s);
      return m ? m[1] : '';
    case 'macOS':
      // Safari and Chrome freeze this at 10_15_7; anything else is real.
      m = /Mac OS X (\d+)[_.](\d+)/.exec(s);
      if (!m || (m[1] === '10' && m[2] === '15')) {
        return '';
      }
      return m[1] === '10' ? `10.${m[2]}` : m[1];
    case 'Quest':
      m = /Quest (\d+|Pro)/.exec(s);
      return m ? m[1] : '';
    default:
      return '';
  }
}

/** OS version from the platformVersion client hint. */
function osVersionFromHint(os, pv) {
  const major = Number.parseInt(pv, 10);
  if (!Number.isFinite(major)) {
    return '';
  }
  if (os === 'Windows') {
    // platformVersion 13+ is Windows 11; 1–10 is Windows 10; 0 is 8.1 or older.
    return major >= 13 ? '11' : major >= 1 ? '10' : '≤8.1';
  }
  if (os === 'macOS' || os === 'Android' || os === 'ChromeOS') {
    return String(major);
  }
  return '';
}

/**
 * @param {string | undefined} ua
 * @param {number} [touchPoints] navigator.maxTouchPoints, from the client payload
 * @param {{ brand?: string, version?: string, platformVersion?: string } | null} [hints] UA client hints
 * @returns {{ browser: string, browserVersion: string, os: string, osVersion: string, device: string }}
 */
export function parseUserAgent(ua, touchPoints = 0, hints = null) {
  const s = ua ?? '';
  let browser = 'Other';
  let bver = '';
  for (const [re, name, vre] of BROWSERS) {
    if (re.test(s)) {
      browser = name;
      const m = vre.exec(s);
      bver = m ? m[1] : '';
      break;
    }
  }
  let os = 'Other';
  for (const [re, name] of SYSTEMS) {
    if (re.test(s)) {
      os = name;
      break;
    }
  }
  let device = 'desktop';
  if (os === 'Quest') {
    device = 'vr';
  } else if (os === 'iPadOS' || /Tablet/.test(s) || (os === 'Android' && !/Mobile/.test(s))) {
    device = 'tablet';
  } else if (/Mobi|iPhone|iPod/.test(s)) {
    device = 'mobile';
  } else if (os === 'macOS' && touchPoints > 1) {
    // iPadOS Safari sends a Mac UA by default; only the touch screen gives it away.
    os = 'iPadOS';
    device = 'tablet';
  }

  let over = osVersionFromUa(os, s);
  if (hints) {
    if (hints.brand) {
      for (const [re, name] of HINT_BRANDS) {
        if (re.test(hints.brand)) {
          browser = name;
          break;
        }
      }
    }
    const hv = Number.parseInt(hints.version ?? '', 10);
    if (Number.isFinite(hv)) {
      bver = String(hv);
    }
    const hp = osVersionFromHint(os, hints.platformVersion ?? '');
    if (hp) {
      over = hp;
    }
  }
  return {
    browser,
    browserVersion: bver ? `${browser} ${bver}` : browser,
    os,
    osVersion: over ? `${os} ${over}` : os,
    device,
  };
}
