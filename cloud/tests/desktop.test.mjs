// Phoenix on the desktop: signing in through the browser (a one-time code
// and PKCE), then editing with a token, as the web editor does with a session.

import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { after, before, describe, it } from 'node:test';
import { makePhx, startServer } from './helpers.mjs';

let server;
let alice;
let bob;

before(async () => {
  server = await startServer();
  alice = server.client();
  bob = server.client();
  await alice.signIn('alice', { admin: true });
  await bob.signIn('bob');
});

after(() => server.close());

/** What Phoenix makes before it opens the browser. */
function pkce() {
  const verifier = crypto.randomBytes(32).toString('base64url');
  const challenge = crypto.createHash('sha256').update(verifier).digest('base64url');
  return { verifier, challenge, state: crypto.randomBytes(16).toString('base64url') };
}

/** Signs Phoenix in as the person `client` is in the browser: its token. */
async function desktopToken(client, name = 'Studio PC') {
  const { verifier, challenge, state } = pkce();
  const { redirect } = await client.post('/api/desktop/authorize', { port: 51234, state, challenge, name });
  const back = new URL(redirect);
  assert.equal(back.origin, 'http://127.0.0.1:51234');
  assert.equal(back.pathname, '/callback');
  assert.equal(back.searchParams.get('state'), state);
  const res = await fetch(`${server.base}/api/desktop/token`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ code: back.searchParams.get('code'), verifier }),
  });
  assert.equal(res.status, 200);
  const json = await res.json();
  return { ...json, code: back.searchParams.get('code'), verifier };
}

/** A request as Phoenix makes it: the token, no cookie, no CSRF token, no Origin. */
function asDesktop(token, method, route, { json, body, headers = {} } = {}) {
  const h = { Authorization: `Bearer ${token}`, ...headers };
  let payload = body;
  if (json !== undefined) {
    h['Content-Type'] = 'application/json';
    payload = JSON.stringify(json);
  }
  return fetch(server.base + route, { method, headers: h, body: payload });
}

describe('desktop sign-in', () => {
  it('trades a one-time code and its verifier for a token', async () => {
    const { token, user } = await desktopToken(bob);
    assert.ok(token.length >= 40);
    assert.equal(user.name, 'bob');
    const me = await (await asDesktop(token, 'GET', '/api/me')).json();
    assert.equal(me.user.name, 'bob');
    assert.equal(me.csrf, null);
  });

  it('takes each code once, and only with the right verifier', async () => {
    const first = await desktopToken(bob);
    const again = await fetch(`${server.base}/api/desktop/token`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ code: first.code, verifier: first.verifier }),
    });
    assert.equal(again.status, 400);

    const { challenge, state } = pkce();
    const { redirect } = await bob.post('/api/desktop/authorize', { port: 51234, state, challenge, name: 'x' });
    const code = new URL(redirect).searchParams.get('code');
    const wrong = await fetch(`${server.base}/api/desktop/token`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ code, verifier: pkce().verifier }),
    });
    assert.equal(wrong.status, 400);
  });

  it('asks the browser to allow it, never a token', async () => {
    const { token } = await desktopToken(bob);
    const { challenge, state } = pkce();
    const res = await asDesktop(token, 'POST', '/api/desktop/authorize', { json: { port: 51234, state, challenge } });
    assert.equal(res.status, 403);
    assert.equal(await bob.fails('POST', '/api/desktop/authorize', 400, { json: { port: 80, state, challenge } }), 'bad_request');
  });

  it('opens, locks, saves and drafts documents with the token', async () => {
    const { token } = await desktopToken(bob);
    const phx = await makePhx();
    const made = await asDesktop(token, 'POST', '/api/documents/upload?name=Desk.phx',
      { body: phx, headers: { 'Content-Type': 'application/octet-stream' } });
    assert.equal(made.status, 201);
    const { document } = await made.json();
    const id = document.id;

    const list = await (await asDesktop(token, 'GET', '/api/documents?view=files')).json();
    assert.ok(list.documents.some((d) => d.id === id));
    const file = await asDesktop(token, 'GET', `/api/documents/${id}/file`);
    assert.equal(file.status, 200);
    assert.equal(Number(file.headers.get('X-Phoenix-Revision')), 1);
    await file.arrayBuffer();

    const lock = await (await asDesktop(token, 'POST', `/api/documents/${id}/lock`, { json: {} })).json();
    assert.equal(lock.held, true);
    // Bob in the browser sees no one else editing (it is him).
    const draft = await asDesktop(token, 'PUT', `/api/documents/${id}/draft`, { body: phx, headers: { 'Content-Type': 'application/octet-stream' } });
    assert.equal(draft.status, 200);
    const saved = await asDesktop(token, 'PUT', `/api/documents/${id}/file?base=1`, { body: phx, headers: { 'Content-Type': 'application/octet-stream' } });
    assert.equal(saved.status, 200);
    assert.equal((await saved.json()).revision, 2);
    // A save on top of an older version is refused, as in the web editor.
    const stale = await asDesktop(token, 'PUT', `/api/documents/${id}/file?base=1`, { body: phx, headers: { 'Content-Type': 'application/octet-stream' } });
    assert.equal(stale.status, 409);
    assert.equal((await asDesktop(token, 'DELETE', `/api/documents/${id}/lock`)).status, 200);
  });

  it('reaches only what the editor needs, and no admin powers', async () => {
    const { token } = await desktopToken(alice);
    assert.equal((await asDesktop(token, 'DELETE', '/api/me')).status, 403);
    assert.equal((await asDesktop(token, 'GET', '/api/me/sessions')).status, 403);
    assert.equal((await asDesktop(token, 'PATCH', '/api/me', { json: { bio: 'x' } })).status, 403);
    assert.equal((await asDesktop(token, 'GET', '/api/admin/users')).status, 403);
    const me = await (await asDesktop(token, 'GET', '/api/me')).json();
    assert.equal(me.user.isAdmin, false);
  });

  it('is listed on the account page, and signs out from there or from Phoenix', async () => {
    const { token } = await desktopToken(bob, 'Laptop');
    const { apps } = await bob.get('/api/me/apps');
    const app = apps.find((a) => a.name === 'Laptop');
    assert.ok(app);
    await bob.del(`/api/me/apps/${app.id}`);
    assert.equal((await asDesktop(token, 'GET', '/api/me')).status, 401);

    const other = await desktopToken(bob, 'Desk');
    assert.equal((await asDesktop(other.token, 'POST', '/api/desktop/signout', { json: {} })).status, 200);
    assert.equal((await asDesktop(other.token, 'GET', '/api/documents')).status, 401);
  });

  it('stops working when the account is suspended', async () => {
    const carol = server.client();
    await carol.signIn('carol');
    const { token } = await desktopToken(carol);
    server.store.run("UPDATE users SET status = 'suspended' WHERE id = ?", carol.user.id);
    assert.equal((await asDesktop(token, 'GET', '/api/me')).status, 401);
  });
});
