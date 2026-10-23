// Telling people when their account is signed in from a browser it has not
// been used in before. Each browser keeps a random cookie of its own, apart
// from the session's; each account keeps the ones it has signed in from
// (known_devices, hashed). The very first sign-in is not news.

import { addCookie, parseCookies, serializeCookie } from './http.mjs';
import { hashToken, newToken } from './ids.mjs';
import { clientAddress } from './limits.mjs';
import { notify } from './notify.mjs';

const KEEP_DAYS = 400;

/** "Firefox on Windows", from a User-Agent. */
export function browserName(ua = '') {
  const browser = /Edg\//.test(ua) ? 'Edge' : /OPR\//.test(ua) ? 'Opera' : /Firefox\//.test(ua) ? 'Firefox'
    : /Chrome\//.test(ua) ? 'Chrome' : /Safari\//.test(ua) ? 'Safari' : 'A browser';
  const system = /iPhone|iPad/.test(ua) ? 'iOS' : /Android/.test(ua) ? 'Android' : /Windows/.test(ua) ? 'Windows'
    : /Mac OS X|Macintosh/.test(ua) ? 'macOS' : /CrOS/.test(ua) ? 'ChromeOS' : /Linux/.test(ua) ? 'Linux' : null;
  return system ? `${browser} on ${system}` : browser;
}

/**
 * Notes that `user` just signed in from this request's browser, and tells
 * them (always, and by email) when it is one they have not used before.
 */
export function noteSignIn(app, req, res, user, how = 'password') {
  const { store, config } = app;
  const name = config.secureCookies ? '__Host-phoenix_device' : 'phoenix_device';
  let token = parseCookies(req.headers.cookie)[name];
  if (!token || !/^[A-Za-z0-9_-]{20,}$/.test(token)) {
    token = newToken();
  }
  addCookie(res, serializeCookie(name, token, { maxAge: KEEP_DAYS * 86400, secure: config.secureCookies }));
  const hash = hashToken(token);
  const now = Date.now();
  const ua = String(req.headers['user-agent'] ?? '').slice(0, 300);
  const address = clientAddress(req, config.trustProxy);
  const known = store.get('SELECT 1 FROM known_devices WHERE user_id = ? AND device_hash = ?', user.id, hash);
  const first = !store.get('SELECT 1 FROM known_devices WHERE user_id = ? LIMIT 1', user.id);
  store.run(`INSERT INTO known_devices (user_id, device_hash, user_agent, address, first_seen, last_seen) VALUES (?, ?, ?, ?, ?, ?)
             ON CONFLICT (user_id, device_hash) DO UPDATE SET last_seen = excluded.last_seen, address = excluded.address, user_agent = excluded.user_agent`,
  user.id, hash, ua, address, now, now);
  if (known || first) {
    return false;
  }
  const where = `${browserName(ua)}${address ? `, from the address ${address}` : ''}`;
  const when = new Date(now).toUTCString();
  store.audit(user.id, 'user.new_device', 'user', user.id, { browser: browserName(ua), address, how });
  notify(app, [user.id], {
    kind: 'signin',
    text: `New sign-in to your account: ${where}`,
    link: '/account',
    email: {
      subject: 'New sign-in to your Phoenix Cloud account',
      text: [
        `Your Phoenix Cloud account was just signed in to from a browser it has not been used in before:`,
        '',
        `  ${browserName(ua)}`,
        address ? `  Address: ${address}` : null,
        `  When: ${when}`,
        `  How: ${how === 'provider' ? 'with a sign-in provider' : how === 'reset' ? 'after a password reset' : 'with your password'}`,
        '',
        'If this was you, there is nothing to do.',
        'If it was not, change your password now, sign out the other browsers (Account > Where you are signed in), and turn on two-step sign-in:',
      ].filter((l) => l !== null).join('\n'),
    },
  });
  return true;
}
