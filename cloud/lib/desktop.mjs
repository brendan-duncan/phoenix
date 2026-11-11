// Phoenix on the desktop, signed in to Phoenix Cloud.
//
// Phoenix asks the person's browser to approve it: it listens on a port of
// its own computer (127.0.0.1), and opens /desktop?port=&state=&challenge=&name=
// in the browser. There, signed in as usual, the person allows it; the page
// asks POST /api/desktop/authorize for a one-time code and sends the browser
// to http://127.0.0.1:<port>/callback?code=&state=. Phoenix trades the code,
// with the secret its challenge was made from (PKCE, S256), for a token at
// POST /api/desktop/token, and calls the API with `Authorization: Bearer`.
//
// A token is stored only as a hash, lasts while it is used (like a session),
// is listed on the account page to sign out, and reaches only what the
// editor needs: documents, folders, starters, fonts and who is signed in,
// never the account itself or the admin tools.

import crypto from 'node:crypto';
import { HttpError, badRequest } from './http.mjs';
import { hashToken, newToken, sameSecret } from './ids.mjs';
import { selfUser } from './accounts.mjs';

const MINUTE = 60 * 1000;
const DAY = 24 * 60 * MINUTE;
export const CODE_MINUTES = 5;
export const TOKEN_DAYS = 180;
const TOUCH_INTERVAL = 5 * MINUTE;

/** The bearer token of a request, or null. */
export function bearerOf(req) {
  const header = String(req.headers.authorization ?? '');
  const match = /^Bearer\s+([A-Za-z0-9_-]{20,200})$/.exec(header.trim());
  return match ? match[1] : null;
}

/** The token's row and its user, or null; a token in use is kept longer. */
export function tokenAuth(store, token) {
  const row = store.get('SELECT * FROM app_tokens WHERE id_hash = ?', hashToken(token));
  const now = Date.now();
  if (!row || row.expires_at < now) {
    return null;
  }
  const user = store.get('SELECT * FROM users WHERE id = ?', row.user_id);
  if (!user || user.status !== 'active') {
    return null;
  }
  if (now - row.last_seen_at > TOUCH_INTERVAL) {
    store.run('UPDATE app_tokens SET last_seen_at = ?, expires_at = ? WHERE id_hash = ?', now, now + TOKEN_DAYS * DAY, row.id_hash);
    store.run('UPDATE users SET last_seen_at = ? WHERE id = ?', now, user.id);
  }
  return { session: null, token: row, user };
}

// What a token may call, by method: the editor's needs, nothing about the
// account itself (its email, password, sessions, deletion) or other people.
const TOKEN_ROUTES = [
  ['GET', /^\/api\/me$/],
  ['GET', /^\/api\/groups$/],
  ['GET', /^\/api\/fonts(\/[^/]+)?$/],
  ['GET', /^\/api\/starters(\/(templates|tutorials)\/[^/]+\/thumbnail)?$/],
  ['GET', /^\/api\/folders$/],
  ['GET', /^\/api\/documents$/],
  ['POST', /^\/api\/documents$/],
  ['POST', /^\/api\/documents\/upload$/],
  ['GET', /^\/api\/documents\/[^/]+$/],
  ['GET', /^\/api\/documents\/[^/]+\/(file|thumbnail|preview|revisions|draft|draft\/file)$/],
  ['PUT', /^\/api\/documents\/[^/]+\/(file|draft)$/],
  ['DELETE', /^\/api\/documents\/[^/]+\/(draft|lock)$/],
  ['POST', /^\/api\/documents\/[^/]+\/(lock|opened)$/],
  ['POST', /^\/api\/desktop\/signout$/],
];

/** Whether a token may make this call. */
export function tokenMay(method, pathname) {
  const m = method === 'HEAD' ? 'GET' : method;
  return TOKEN_ROUTES.some(([allowed, re]) => allowed === m && re.test(pathname));
}

/** The S256 challenge of a PKCE verifier. */
export function challengeOf(verifier) {
  return crypto.createHash('sha256').update(String(verifier)).digest('base64url');
}

function cleanName(value) {
  return String(value ?? '').replace(/[\u0000-\u001f]+/g, ' ').trim().slice(0, 80) || 'Phoenix';
}

export function registerDesktop(router, app) {
  const { store } = app;

  // The browser's half: the person, signed in with a session, allows
  // Phoenix on their computer. Only from a browser session, never a token.
  router.post('/api/desktop/authorize', async (ctx) => {
    const user = ctx.requireUser();
    if (!ctx.session) {
      throw new HttpError(403, 'browser_only', 'Allow Phoenix from Phoenix Cloud in your browser.');
    }
    const body = await ctx.json();
    const port = Number(body.port);
    if (!Number.isInteger(port) || port < 1024 || port > 65535) {
      throw badRequest('The port is a number from 1024 to 65535.');
    }
    const state = String(body.state ?? '');
    if (!/^[A-Za-z0-9_-]{16,128}$/.test(state)) {
      throw badRequest('The state is 16 to 128 letters, digits, - or _.');
    }
    const challenge = String(body.challenge ?? '');
    if (!/^[A-Za-z0-9_-]{43}$/.test(challenge)) {
      throw badRequest('The challenge is an S256 PKCE challenge.');
    }
    const code = newToken();
    store.run('DELETE FROM app_codes WHERE expires_at < ?', Date.now());
    store.run('INSERT INTO app_codes (code_hash, user_id, challenge, name, expires_at) VALUES (?, ?, ?, ?, ?)',
      hashToken(code), user.id, challenge, cleanName(body.name), Date.now() + CODE_MINUTES * MINUTE);
    store.audit(user.id, 'desktop.authorize', 'user', user.id, { name: cleanName(body.name) });
    const back = new URL(`http://127.0.0.1:${port}/callback`);
    back.searchParams.set('code', code);
    back.searchParams.set('state', state);
    return { redirect: back.toString() };
  });

  // Phoenix's half: the code and its verifier for a token. Each code works
  // once, for five minutes.
  const exchange = async (ctx) => {
    const body = await ctx.json();
    const code = String(body.code ?? '');
    const verifier = String(body.verifier ?? '');
    const row = /^[A-Za-z0-9_-]{20,200}$/.test(code)
      ? store.get('SELECT * FROM app_codes WHERE code_hash = ?', hashToken(code)) : null;
    if (row) {
      store.run('DELETE FROM app_codes WHERE code_hash = ?', row.code_hash);
    }
    if (!row || row.expires_at < Date.now() || !/^[A-Za-z0-9._~-]{43,128}$/.test(verifier) || !sameSecret(challengeOf(verifier), row.challenge)) {
      throw new HttpError(400, 'code', 'That sign-in took too long or was already used. Sign in again from Phoenix.');
    }
    const user = store.get('SELECT * FROM users WHERE id = ?', row.user_id);
    if (!user || user.status !== 'active') {
      throw new HttpError(403, 'account', 'That account cannot sign in now.');
    }
    const token = newToken();
    const now = Date.now();
    store.run('INSERT INTO app_tokens (id_hash, user_id, name, created_at, last_seen_at, expires_at) VALUES (?, ?, ?, ?, ?, ?)',
      hashToken(token), user.id, row.name, now, now, now + TOKEN_DAYS * DAY);
    return { token, user: selfUser(user) };
  };
  exchange.anonymous = true;
  router.post('/api/desktop/token', exchange);

  // Phoenix signing out: its token goes.
  router.post('/api/desktop/signout', (ctx) => {
    if (ctx.appToken) {
      store.run('DELETE FROM app_tokens WHERE id_hash = ?', ctx.appToken.id_hash);
    }
    return { ok: true };
  });

  // The account page: where Phoenix is signed in, to sign it out.
  router.get('/api/me/apps', (ctx) => {
    const user = ctx.requireUser();
    return {
      apps: store.all('SELECT * FROM app_tokens WHERE user_id = ? AND expires_at > ? ORDER BY last_seen_at DESC', user.id, Date.now())
        .map((t) => ({ id: t.id_hash.slice(0, 16), name: t.name, createdAt: t.created_at, lastSeenAt: t.last_seen_at })),
    };
  });

  router.delete('/api/me/apps/:id', (ctx) => {
    const user = ctx.requireUser();
    const id = ctx.params.id;
    if (!/^[0-9a-f]{16}$/.test(id) ||
      store.run('DELETE FROM app_tokens WHERE user_id = ? AND substr(id_hash, 1, 16) = ?', user.id, id).changes === 0) {
      throw new HttpError(404, 'not_found', 'No such sign-in.');
    }
    return { ok: true };
  });
}

/** Tokens and codes past their time. */
export function purgeDesktop(store) {
  const now = Date.now();
  store.run('DELETE FROM app_tokens WHERE expires_at < ?', now);
  store.run('DELETE FROM app_codes WHERE expires_at < ?', now);
}
