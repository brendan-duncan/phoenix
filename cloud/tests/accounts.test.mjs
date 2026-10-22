// Registering under each new-accounts setting, confirming an email address,
// invites, and what admins do to accounts.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { createInvite } from '../lib/accounts.mjs';
import { makePhx, startServer } from './helpers.mjs';

let server;
let admin;

before(async () => {
  server = await startServer();
  admin = server.client();
  await admin.signInWith('phoenix', 'animate');
});

after(() => server.close());

function register(client, handle, extra = {}) {
  return client.request('POST', '/api/auth/register', { json: { handle, name: handle, email: `${handle}@example.com`, password: 'a-good-password', ...extra } });
}

async function setNewAccounts(value) {
  await admin.patch('/api/admin/settings', { newAccounts: value });
}

/** The token in the newest email to `to`. */
function confirmToken(to) {
  const mail = server.app.mailer.recent().find((m) => m.to === to);
  return /\/confirm\/([A-Za-z0-9_-]+)/.exec(mail?.text ?? '')?.[1];
}

describe('registering', () => {
  it('waits for an admin by default', async () => {
    const c = server.client();
    const res = await register(c, 'hana');
    assert.equal(res.status, 201);
    assert.equal((await res.json()).status, 'pending_admin');
    assert.equal(await c.fails('POST', '/api/auth/login', 403, { json: { login: 'hana', password: 'a-good-password' } }), 'pending_admin');
    const { users } = await admin.get('/api/admin/users');
    const hana = users.find((u) => u.handle === 'hana');
    assert.equal(hana.status, 'pending_admin');
    await admin.patch(`/api/admin/users/${hana.id}`, { status: 'active' });
    await c.signInWith('hana', 'a-good-password');
    assert.equal(c.user.handle, 'hana');
  });

  it('is ready at once when accounts are open', async () => {
    await setNewAccounts('open');
    const c = server.client();
    const res = await register(c, 'ivan');
    assert.equal((await res.json()).status, 'active');
    // Registering signs them in.
    assert.equal((await c.get('/api/me')).user.handle, 'ivan');
    await setNewAccounts('admin');
  });

  it('waits for the email link when confirmation is on', async () => {
    await setNewAccounts('email');
    const c = server.client();
    assert.equal((await (await register(c, 'jun')).json()).status, 'pending_email');
    assert.equal(await c.fails('POST', '/api/auth/login', 403, { json: { login: 'jun@example.com', password: 'a-good-password' } }), 'pending_email');
    const token = confirmToken('jun@example.com');
    assert.ok(token);
    const confirmed = await c.post('/api/auth/confirm', { token });
    assert.equal(confirmed.status, 'active');
    assert.equal((await c.get('/api/me')).user.handle, 'jun');
    // A link works once.
    assert.equal(await server.client().fails('POST', '/api/auth/confirm', 400, { json: { token } }), 'token');
    // The admin page shows what was sent.
    assert.ok((await admin.get('/api/admin/outbox')).messages.some((m) => m.to === 'jun@example.com'));
    await setNewAccounts('admin');
  });

  it('lets someone with an invite in at once, and holds it to its email', async () => {
    const { token } = createInvite(server.store, { email: 'kai@example.com' });
    const wrong = server.client();
    assert.equal(await wrong.fails('POST', '/api/auth/register', 400, { json: { handle: 'kai', email: 'other@example.com', password: 'a-good-password', invite: token } }), 'invite_email');
    const c = server.client();
    assert.equal((await (await register(c, 'kai', { invite: token })).json()).status, 'active');
    assert.equal((await c.get('/api/me')).user.handle, 'kai');
    assert.equal(await server.client().fails('POST', '/api/auth/register', 400, { json: { handle: 'kai2', email: 'kai@example.com', password: 'a-good-password', invite: token } }), 'invite_invalid');
  });

  it('checks names, addresses and passwords', async () => {
    const c = server.client();
    assert.equal((await register(c, 'hana')).status, 409);
    assert.equal((await register(c, 'lou', { email: 'hana@example.com' })).status, 409);
    assert.equal((await register(c, 'm')).status, 400);
    assert.equal((await register(c, 'lou', { email: 'not-an-email' })).status, 400);
    assert.equal((await register(c, 'lou', { password: 'short' })).status, 400);
  });
});

describe('admin account controls', () => {
  it('makes accounts that are ready to use', async () => {
    const { user } = await admin.post('/api/admin/users', { handle: 'max', name: 'Max', email: 'max@example.com', password: 'made-by-admin', isAdmin: false });
    assert.equal(user.status, 'active');
    await server.client().signInWith('max', 'made-by-admin');
    assert.equal(await admin.fails('POST', '/api/admin/users', 409, { json: { handle: 'max', password: 'whatever-123' } }), 'taken');
  });

  it('sets a password, which signs the account out', async () => {
    const c = server.client();
    await c.signInWith('max', 'made-by-admin');
    const id = c.user.id;
    await admin.patch(`/api/admin/users/${id}`, { password: 'reset-by-admin' });
    assert.equal((await c.get('/api/me')).user, null);
    await server.client().signInWith('max', 'reset-by-admin');
  });

  it('suspends and unsuspends', async () => {
    const c = server.client();
    await c.signInWith('max', 'reset-by-admin');
    await admin.patch(`/api/admin/users/${c.user.id}`, { status: 'suspended' });
    assert.equal(await server.client().fails('POST', '/api/auth/login', 403, { json: { login: 'max', password: 'reset-by-admin' } }), 'suspended');
    await admin.patch(`/api/admin/users/${c.user.id}`, { status: 'active' });
    await server.client().signInWith('max', 'reset-by-admin');
  });

  it('deletes an account and what it owns, but not the last admin of a shared group', async () => {
    const nia = server.client();
    await nia.signIn('nia');
    const { document: doc } = await nia.upload('Gone', await makePhx({ document: 'gone' }));
    const { group } = await nia.post('/api/groups', { name: 'Nia Crew' });
    await nia.post(`/api/groups/${group.id}/members`, { who: 'max' });
    assert.equal(await admin.fails('DELETE', `/api/admin/users/${nia.user.id}`, 409), 'last_admin');
    const { members } = await nia.get(`/api/groups/${group.id}`);
    const maxId = members.find((m) => m.user.handle === 'max').user.id;
    await nia.patch(`/api/groups/${group.id}/members/${maxId}`, { role: 'admin' });
    await admin.del(`/api/admin/users/${nia.user.id}`);
    assert.equal(server.store.get('SELECT 1 FROM documents WHERE id = ?', doc.id), undefined);
    assert.equal(await server.client().fails('POST', '/api/auth/login', 401, { json: { login: 'nia', password: 'password-123' } }), 'bad_login');
  });

  it('keeps admins from locking themselves out', async () => {
    const me = admin.user;
    assert.equal(await admin.fails('PATCH', `/api/admin/users/${me.id}`, 403, { json: { status: 'suspended' } }), 'forbidden');
    assert.equal(await admin.fails('DELETE', `/api/admin/users/${me.id}`, 403), 'forbidden');
  });

  it('takes only known settings', async () => {
    assert.equal(await admin.fails('PATCH', '/api/admin/settings', 400, { json: { newAccounts: 'everyone' } }), 'bad_request');
    assert.equal((await admin.get('/api/admin/settings')).settings.newAccounts, 'admin');
  });
});
