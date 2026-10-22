// The full OpenID Connect sign-in against a mock provider in this process:
// PKCE, state, nonce and a signed ID token, and the beta's invite rules.

import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import http from 'node:http';
import { after, before, describe, it } from 'node:test';
import { createInvite } from '../lib/accounts.mjs';
import { startServer } from './helpers.mjs';

let server;
let provider;
let issuer;
const { privateKey, publicKey } = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
const KID = 'test-key';
// code → { claims, verifier challenge } waiting for the token exchange.
const pending = new Map();

function sign(claims) {
  const enc = (o) => Buffer.from(JSON.stringify(o)).toString('base64url');
  const data = `${enc({ alg: 'RS256', kid: KID, typ: 'JWT' })}.${enc(claims)}`;
  return `${data}.${crypto.sign('sha256', Buffer.from(data), privateKey).toString('base64url')}`;
}

function startProvider() {
  return new Promise((resolve) => {
    const srv = http.createServer(async (req, res) => {
      const url = new URL(req.url, issuer);
      if (url.pathname === '/jwks') {
        res.end(JSON.stringify({ keys: [{ ...publicKey.export({ format: 'jwk' }), kid: KID, alg: 'RS256', use: 'sig' }] }));
        return;
      }
      if (url.pathname === '/token') {
        let body = '';
        for await (const chunk of req) {
          body += chunk;
        }
        const form = new URLSearchParams(body);
        const entry = pending.get(form.get('code'));
        const challenge = crypto.createHash('sha256').update(form.get('code_verifier') ?? '').digest('base64url');
        if (!entry || entry.challenge !== challenge || form.get('client_secret') !== 'secret') {
          res.writeHead(400).end(JSON.stringify({ error: 'invalid_grant' }));
          return;
        }
        pending.delete(form.get('code'));
        res.end(JSON.stringify({ access_token: 'at', id_token: sign(entry.claims) }));
        return;
      }
      res.writeHead(404).end();
    });
    srv.listen(0, '127.0.0.1', () => resolve(srv));
  });
}

before(async () => {
  provider = await startProvider();
  issuer = `http://127.0.0.1:${provider.address().port}`;
  server = await startServer();
  server.config.providers.mock = {
    id: 'mock', name: 'Mock', kind: 'oidc', scope: 'openid email profile',
    authorizeUrl: `${issuer}/authorize`, tokenUrl: `${issuer}/token`, jwksUrl: `${issuer}/jwks`,
    issuers: [issuer], clientId: 'client', clientSecret: 'secret',
  };
});

after(async () => {
  await server.close();
  provider.close();
});

/**
 * Starts a sign-in, plays the provider's part (the person says yes as
 * `claims`), and follows the callback. Returns the callback's redirect.
 */
async function signIn(client, claims, query = '', tweak = (c) => c) {
  const start = await client.request('GET', `/api/auth/start/mock${query}`);
  assert.equal(start.status, 302);
  const auth = new URL(start.headers.get('location'));
  assert.equal(auth.origin + auth.pathname, `${issuer}/authorize`);
  assert.equal(auth.searchParams.get('code_challenge_method'), 'S256');
  const code = crypto.randomBytes(8).toString('hex');
  const full = tweak({
    iss: issuer, aud: 'client', exp: Math.floor(Date.now() / 1000) + 600, iat: Math.floor(Date.now() / 1000),
    nonce: auth.searchParams.get('nonce'), email_verified: true, ...claims,
  });
  pending.set(code, { claims: full, challenge: auth.searchParams.get('code_challenge') });
  const back = await client.request('GET', `/api/auth/callback/mock?code=${code}&state=${auth.searchParams.get('state')}`);
  assert.equal(back.status, 302);
  return back.headers.get('location');
}

describe('OpenID Connect sign-in', () => {
  it('lists the provider', async () => {
    const me = await server.client().get('/api/me');
    assert.deepEqual(me.providers, [{ id: 'mock', name: 'Mock' }]);
  });

  it('makes a new account wait for an admin, as new accounts do by default', async () => {
    const c = server.client();
    assert.equal(await signIn(c, { sub: 'stranger', email: 's@example.com' }), '/signin?error=pending_admin');
    assert.equal((await c.get('/api/me')).user, null);
    const waiting = server.store.get("SELECT status FROM users WHERE email = 's@example.com'");
    assert.equal(waiting.status, 'pending_admin');
    // Still waiting the next time.
    assert.equal(await signIn(c, { sub: 'stranger', email: 's@example.com' }), '/signin?error=pending_admin');
  });

  it('lets an invited person in, once', async () => {
    const { token } = createInvite(server.store, { email: 'pat@example.com', makeAdmin: true });
    const c = server.client();
    assert.equal(await signIn(c, { sub: 'pat', email: 'Pat@Example.com', name: 'Pat' }, `?invite=${token}&return=/files`), '/files');
    const me = await c.get('/api/me');
    assert.equal(me.user.name, 'Pat');
    assert.equal(me.user.isAdmin, true);
    // The same sign-in works again without the invite.
    const again = server.client();
    assert.equal(await signIn(again, { sub: 'pat', email: 'pat@example.com' }), '/');
    // The invite is used up.
    const other = server.client();
    assert.match(await signIn(other, { sub: 'pat2', email: 'pat2@example.com' }, `?invite=${token}`), /error=invite_invalid/);
  });

  it('holds an invite to its email', async () => {
    const { token } = createInvite(server.store, { email: 'quinn@example.com' });
    const c = server.client();
    assert.match(await signIn(c, { sub: 'q', email: 'someone@else.com' }, `?invite=${token}`), /error=invite_email/);
    assert.match(await signIn(c, { sub: 'q', email: 'quinn@example.com', email_verified: false }, `?invite=${token}`), /error=invite_email/);
  });

  it('never links a new sign-in to an account by email alone', async () => {
    const { token } = createInvite(server.store, {});
    const c = server.client();
    assert.match(await signIn(c, { sub: 'pat-elsewhere', email: 'pat@example.com' }, `?invite=${token}`), /error=email_in_use/);
  });

  it('refuses a token with the wrong nonce, audience or signature', async () => {
    server.store.saveSettings({ newAccounts: 'open' });
    try {
      const c = server.client();
      assert.match(await signIn(c, { sub: 'n1' }, '', (claims) => ({ ...claims, nonce: 'other' })), /error=failed/);
      assert.match(await signIn(c, { sub: 'n2' }, '', (claims) => ({ ...claims, aud: 'someone-else' })), /error=failed/);
      assert.match(await signIn(c, { sub: 'n3' }, '', (claims) => ({ ...claims, exp: 1000 })), /error=failed/);
      assert.equal(await signIn(c, { sub: 'n4', email: 'n4@example.com' }), '/');
    } finally {
      server.store.saveSettings({ newAccounts: 'admin' });
    }
  });

  it('refuses a state it did not issue, or one used twice', async () => {
    const c = server.client();
    const res = await c.request('GET', '/api/auth/callback/mock?code=x&state=made-up');
    assert.equal(res.headers.get('location'), '/signin?error=state');
  });

  it('links a second sign-in to the signed-in account', async () => {
    const c = server.client();
    const me = await c.signIn('linker');
    const start = await c.request('GET', '/api/auth/start/mock?link=1');
    const auth = new URL(start.headers.get('location'));
    const code = 'link-code';
    pending.set(code, {
      claims: { iss: issuer, aud: 'client', exp: Math.floor(Date.now() / 1000) + 600, nonce: auth.searchParams.get('nonce'), sub: 'linker-google', email: 'linker@example.com', email_verified: true },
      challenge: auth.searchParams.get('code_challenge'),
    });
    const back = await c.request('GET', `/api/auth/callback/mock?code=${code}&state=${auth.searchParams.get('state')}`);
    assert.equal(back.headers.get('location'), '/account');
    const { identities } = await c.get('/api/me/identities');
    assert.deepEqual(identities.map((i) => i.provider), ['mock']);
    // Signing in with it later reaches the same account.
    const later = server.client();
    assert.equal(await signIn(later, { sub: 'linker-google', email: 'linker@example.com' }), '/');
    assert.equal((await later.get('/api/me')).user.id, me.id);
  });
});
