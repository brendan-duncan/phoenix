// Keeping bots from filling the admins' list and spending email: new
// accounts an hour from one address and on the whole site, and a form field
// only bots fill in.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { startServer } from './helpers.mjs';

const form = (n) => ({ name: `newcomer${n}`, email: `newcomer${n}@example.com`, password: 'a long password', message: 'Hello, I animate.' });

describe('sign-ups from one address', () => {
  let server;
  let admin;

  before(async () => {
    server = await startServer({ env: { PHOENIX_CLOUD_SIGNUPS_PER_ADDRESS_PER_HOUR: '3', PHOENIX_CLOUD_SIGNUPS_PER_HOUR: '100' } });
    admin = server.client();
    await admin.signInWith('phoenix', 'animate');
  });

  after(() => server.close());

  it('are so many an hour, and an invite still lets someone in', async () => {
    for (let n = 1; n <= 3; n++) {
      await server.client().ok('POST', '/api/auth/register', { json: form(n) });
    }
    assert.equal(await server.client().fails('POST', '/api/auth/register', 429, { json: form(4) }), 'slow_down');
    const { url } = await admin.post('/api/admin/invites', { send: false });
    const invite = url.split('/').pop();
    await server.client().ok('POST', '/api/auth/register', { json: { ...form(5), invite } });
  });

  it('take nothing from a bot that fills in the hidden field', async () => {
    const before = server.store.get('SELECT COUNT(*) AS n FROM users').n;
    const answer = await server.client().ok('POST', '/api/auth/register', { json: { ...form(6), website: 'http://spam.example' } });
    assert.equal(answer.status, 'pending_admin');
    assert.equal(server.store.get('SELECT COUNT(*) AS n FROM users').n, before);
  });
});

describe('sign-ups on the whole site', () => {
  let server;

  before(async () => {
    server = await startServer({ env: { PHOENIX_CLOUD_SIGNUPS_PER_ADDRESS_PER_HOUR: '100', PHOENIX_CLOUD_SIGNUPS_PER_HOUR: '2' } });
  });

  after(() => server.close());

  it('are so many an hour, whoever asks', async () => {
    await server.client().ok('POST', '/api/auth/register', { json: form(1) });
    await server.client().ok('POST', '/api/auth/register', { json: form(2) });
    const res = await server.client().request('POST', '/api/auth/register', { json: form(3) });
    assert.equal(res.status, 429);
    assert.match((await res.json()).message, /a lot of new accounts right now/);
  });
});
