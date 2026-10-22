// Drafts, edit locks, notifications, password resets, profile pictures,
// version thumbnails, where search results are, and the font library.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { makePhx, startServer } from './helpers.mjs';

let server;
let ann;
let ben;
let admin;
let doc;
let bytes;

const octets = { 'Content-Type': 'application/octet-stream' };

before(async () => {
  server = await startServer();
  ann = server.client();
  ben = server.client();
  admin = server.client();
  await ann.signIn('ann');
  await ben.signIn('ben');
  await admin.signInWith('phoenix', 'animate');
  bytes = await makePhx({ symbols: ['One', 'Two'] });
  ({ document: doc } = await ann.upload('Shared scene.phx', bytes));
  await ann.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'ben', role: 'editor' });
});

after(() => server.close());

describe('drafts', () => {
  it('keep each person\'s unsaved work until they save', async () => {
    const draft = await makePhx({ symbols: ['One', 'Two', 'Three'] });
    await ann.ok('PUT', `/api/documents/${doc.id}/draft`, { body: draft, headers: octets });
    const { draft: info } = await ann.get(`/api/documents/${doc.id}/draft`);
    assert.equal(info.baseRevision, 1);
    const res = await ann.request('GET', `/api/documents/${doc.id}/draft/file`);
    assert.ok(Buffer.from(await res.arrayBuffer()).equals(draft));
    // Ben has none of his own.
    assert.equal((await ben.get(`/api/documents/${doc.id}/draft`)).draft, null);
    await ann.ok('PUT', `/api/documents/${doc.id}/file`, { body: draft, headers: octets });
    assert.equal((await ann.get(`/api/documents/${doc.id}/draft`)).draft, null);
  });
});

describe('edit locks', () => {
  it('let one person edit at a time', async () => {
    assert.deepEqual(await ann.post(`/api/documents/${doc.id}/lock`), { held: true });
    const theirs = await ben.post(`/api/documents/${doc.id}/lock`);
    assert.equal(theirs.held, false);
    assert.equal(theirs.holder.name, 'ann');
    assert.equal((await ben.get(`/api/documents/${doc.id}`)).document.editing.name, 'ann');
    assert.equal(await ben.fails('PUT', `/api/documents/${doc.id}/file`, 423, { body: bytes, headers: octets }), 'locked');
    await ann.del(`/api/documents/${doc.id}/lock`);
    assert.deepEqual(await ben.post(`/api/documents/${doc.id}/lock`), { held: true });
    await ben.ok('PUT', `/api/documents/${doc.id}/file`, { body: bytes, headers: octets });
    await ben.del(`/api/documents/${doc.id}/lock`);
  });
});

describe('notifications', () => {
  it('tell people what was shared with them, and count what is unread', async () => {
    const { notifications, unread } = await ben.get('/api/notifications');
    assert.ok(unread >= 1);
    assert.match(notifications[0].text, /ann shared “Shared scene” with you/);
    assert.equal(notifications[0].link, `/d/${doc.id}`);
    assert.equal((await ben.get('/api/me')).unread, unread);
    assert.equal((await ben.post('/api/notifications/read')).unread, 0);
  });

  it('tell people they were added to a group', async () => {
    const { group } = await ann.post('/api/groups', { name: 'Ink' });
    await ann.post(`/api/groups/${group.id}/members`, { who: 'ben' });
    const { notifications } = await ben.get('/api/notifications');
    assert.match(notifications[0].text, /added you to Ink/);
  });

  it('tell admins about accounts waiting, and people when theirs is unlocked', async () => {
    const res = await server.client().request('POST', '/api/auth/register', { json: { name: 'cleo', email: 'cleo@example.com', password: 'a-good-password', message: 'Hi there.' } });
    assert.equal(res.status, 201);
    const forAdmin = await admin.get('/api/notifications');
    assert.match(forAdmin.notifications[0].text, /cleo asked for an account/);
    const cleo = (await admin.get('/api/admin/users')).users.find((u) => u.name === 'cleo');
    await admin.patch(`/api/admin/users/${cleo.id}`, { status: 'active' });
    const c = server.client();
    await c.signInWith('cleo', 'a-good-password');
    assert.match((await c.get('/api/notifications')).notifications[0].text, /account is ready/);
    assert.ok(server.app.mailer.recent().some((m) => m.to === 'cleo@example.com' && /ready/.test(m.subject)));
  });
});

describe('forgotten passwords', () => {
  it('can be reset from an emailed link, once', async () => {
    assert.deepEqual(await server.client().post('/api/auth/forgot', { email: 'ann@example.com' }), { ok: true });
    assert.deepEqual(await server.client().post('/api/auth/forgot', { email: 'nobody@example.com' }), { ok: true });
    const mail = server.app.mailer.recent().find((m) => m.to === 'ann@example.com');
    const token = /\/reset\/([A-Za-z0-9_-]+)/.exec(mail.text)[1];
    const c = server.client();
    await c.post('/api/auth/reset', { token, password: 'brand-new-pass' });
    assert.equal((await c.get('/api/me')).user.name, 'ann');
    // Her old session is gone; the new password works; the link is used up.
    assert.equal((await ann.get('/api/me')).user, null);
    await ann.signInWith('ann', 'brand-new-pass');
    assert.equal(await server.client().fails('POST', '/api/auth/reset', 400, { json: { token, password: 'another-pass' } }), 'token');
  });
});

describe('profile pictures', () => {
  it('take an image and give it back', async () => {
    const png = Buffer.from('89504e470d0a1a0a0000000d4948445200000001000000010806000000', 'hex');
    const { user } = await ben.ok('PUT', '/api/me/avatar', { body: png, headers: octets });
    assert.match(user.avatarUrl, /^\/api\/users\/.+\/avatar\?v=/);
    const res = await ann.request('GET', user.avatarUrl);
    assert.equal(res.headers.get('content-type'), 'image/png');
    assert.equal(await ben.fails('PUT', '/api/me/avatar', 400, { body: Buffer.from('not a picture'), headers: octets }), 'bad_request');
    assert.equal((await ben.del('/api/me/avatar')).user.avatarUrl, '');
  });
});

describe('versions and search', () => {
  it('show each version\'s thumbnail', async () => {
    const { revisions } = await ann.get(`/api/documents/${doc.id}/revisions`);
    assert.ok(revisions.every((r) => r.thumbnailUrl));
    assert.equal((await ann.request('GET', revisions[0].thumbnailUrl)).headers.get('content-type'), 'image/png');
  });

  it('say where each result is', async () => {
    const { folder } = await ann.post('/api/folders', { name: 'Scenes' });
    await ann.upload('Deep scene.phx', await makePhx({ document: 'deep' }), `&folderId=${folder.id}`);
    const { documents } = await ann.get('/api/documents?q=deep');
    assert.equal(documents[0].where, 'My Files / Scenes');
    const shared = (await ben.get('/api/documents?q=shared')).documents[0];
    assert.equal(shared.where, 'ann');
  });
});

describe('the font library', () => {
  it('is listed for the editor, and admins add and remove fonts', async () => {
    assert.deepEqual((await ann.get('/api/fonts')).fonts, []);
    const font = Buffer.concat([Buffer.from([0, 1, 0, 0]), Buffer.alloc(60)]);
    await admin.ok('POST', '/api/admin/fonts?name=My Font.ttf', { body: font, headers: octets });
    const { fonts } = await ann.get('/api/fonts');
    assert.deepEqual(fonts.map((f) => f.name), ['My_Font.ttf']);
    assert.ok(Buffer.from(await (await ann.request('GET', fonts[0].url)).arrayBuffer()).equals(font));
    assert.equal(await admin.fails('POST', '/api/admin/fonts?name=evil.exe', 400, { body: font, headers: octets }), 'bad_request');
    assert.equal(await ann.fails('POST', '/api/admin/fonts?name=x.ttf', 403, { body: font, headers: octets }), 'forbidden');
    await admin.del('/api/admin/fonts/My_Font.ttf');
    assert.deepEqual((await ann.get('/api/fonts')).fonts, []);
  });
});
