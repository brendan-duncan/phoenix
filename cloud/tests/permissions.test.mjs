// Who may download what, downloads of several documents as a ZIP, and the
// admin's switches for registering and for locking the site.

import assert from 'node:assert/strict';
import zlib from 'node:zlib';
import { after, before, describe, it } from 'node:test';
import { makePhx, startServer } from './helpers.mjs';

let server;
let owner;
let viewer;
let editor;
let member;
let admin;
let doc;
let bytes;

before(async () => {
  server = await startServer();
  owner = server.client();
  viewer = server.client();
  editor = server.client();
  member = server.client();
  admin = server.client();
  await owner.signIn('olive');
  await viewer.signIn('vic');
  await editor.signIn('ed');
  await member.signIn('meg');
  await admin.signInWith('phoenix', 'animate');
  bytes = await makePhx({ symbols: ['One'] });
  ({ document: doc } = await owner.upload('Walk cycle.phx', bytes));
  await owner.patch(`/api/documents/${doc.id}`, { allowDownload: false });
});

after(() => server.close());

/** The entries of a ZIP: name and bytes (stored entries only, as the server writes). */
function unzip(buf) {
  const end = buf.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
  const count = buf.readUInt16LE(end + 10);
  let at = buf.readUInt32LE(end + 16);
  const out = [];
  for (let i = 0; i < count; i++) {
    assert.equal(buf.readUInt32LE(at), 0x02014b50);
    const crc = buf.readUInt32LE(at + 16);
    const size = buf.readUInt32LE(at + 20);
    const nameLength = buf.readUInt16LE(at + 28);
    const offset = buf.readUInt32LE(at + 42);
    const name = buf.subarray(at + 46, at + 46 + nameLength).toString();
    const start = offset + 30 + buf.readUInt16LE(offset + 26);
    const data = buf.subarray(start, start + size);
    assert.equal(zlib.crc32(data), crc, `${name}'s CRC`);
    out.push({ name, data });
    at += 46 + nameLength;
  }
  return out;
}

describe('download permission', () => {
  it('goes with each share: editors by default, viewers when allowed', async () => {
    await owner.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'vic', role: 'viewer' });
    await owner.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'ed', role: 'editor' });
    const shares = await owner.get(`/api/documents/${doc.id}/shares`);
    assert.deepEqual(shares.users.map((s) => [s.user.name, s.canDownload]), [['vic', false], ['ed', true]]);

    assert.equal((await viewer.get(`/api/documents/${doc.id}`)).document.canDownload, false);
    assert.equal(await viewer.fails('GET', `/api/documents/${doc.id}/download`, 403), 'forbidden');
    assert.equal(await viewer.fails('GET', `/api/documents/${doc.id}/file`, 403), 'forbidden');
    const res = await editor.request('GET', `/api/documents/${doc.id}/download`);
    assert.equal(res.status, 200);
    assert.ok(Buffer.from(await res.arrayBuffer()).equals(bytes));

    // The owner lets the viewer download, and takes it from the editor,
    // who can still open it to edit but not keep a copy.
    await owner.patch(`/api/documents/${doc.id}/shares/user/${viewer.user.id}`, { canDownload: true });
    await owner.patch(`/api/documents/${doc.id}/shares/user/${editor.user.id}`, { canDownload: false });
    assert.equal((await viewer.get(`/api/documents/${doc.id}`)).document.canDownload, true);
    assert.equal((await viewer.request('GET', `/api/documents/${doc.id}/download`)).status, 200);
    const asEditor = (await editor.get(`/api/documents/${doc.id}`)).document;
    assert.deepEqual([asEditor.canOpen, asEditor.canDownload], [true, false]);
    assert.equal(await editor.fails('GET', `/api/documents/${doc.id}/download`, 403), 'forbidden');
    assert.equal((await editor.request('GET', `/api/documents/${doc.id}/file`)).status, 200);
  });

  it('is needed to make a copy', async () => {
    await owner.patch(`/api/documents/${doc.id}`, { allowDuplicate: true });
    await owner.patch(`/api/documents/${doc.id}/shares/user/${viewer.user.id}`, { canDownload: false });
    assert.equal(await viewer.fails('POST', `/api/documents/${doc.id}/duplicate`, 403, { json: {} }), 'forbidden');
    await owner.patch(`/api/documents/${doc.id}/shares/user/${viewer.user.id}`, { canDownload: true });
    await viewer.post(`/api/documents/${doc.id}/duplicate`, {});
  });

  it('goes with a share to a group, for everyone in it', async () => {
    const { group } = await member.post('/api/groups', { name: 'Layout' });
    await member.post(`/api/groups/${group.id}/members`, { who: 'olive' });
    await owner.post(`/api/documents/${doc.id}/shares`, { kind: 'group', who: group.id, role: 'viewer', canDownload: false });
    assert.equal((await member.get(`/api/documents/${doc.id}`)).document.canDownload, false);
    await owner.patch(`/api/documents/${doc.id}/shares/group/${group.id}`, { canDownload: true });
    assert.equal((await member.get(`/api/documents/${doc.id}`)).document.canDownload, true);
    // Changing only the role keeps the download switch.
    await owner.patch(`/api/documents/${doc.id}/shares/group/${group.id}`, { role: 'commenter' });
    const shares = await owner.get(`/api/documents/${doc.id}/shares`);
    assert.deepEqual(shares.groups.map((s) => [s.role, s.canDownload]), [['commenter', true]]);
  });

  it('comes in one ZIP for several documents', async () => {
    const second = await makePhx({ symbols: ['Two'] });
    const { document: other } = await owner.upload('Walk cycle.phx', second);
    const res = await owner.request('GET', `/api/download?ids=${doc.id},${other.id}`);
    assert.equal(res.status, 200);
    assert.equal(res.headers.get('content-type'), 'application/zip');
    const zip = Buffer.from(await res.arrayBuffer());
    assert.equal(Number(res.headers.get('content-length')), zip.length);
    const entries = unzip(zip);
    assert.deepEqual(entries.map((e) => e.name), ['Walk cycle.phx', 'Walk cycle (2).phx']);
    assert.ok(entries[0].data.equals(bytes));
    assert.ok(entries[1].data.equals(second));
    // One the asker may not download refuses the whole.
    assert.equal(await editor.fails('GET', `/api/download?ids=${doc.id}`, 403), 'forbidden');
  });
});

describe('registration and locking', () => {
  it('closes registration to all but invites', async () => {
    await admin.patch('/api/admin/settings', { registration: false, newAccounts: 'open' });
    const stranger = server.client();
    const form = { name: 'newbie', email: 'newbie@example.com', password: 'a good password' };
    assert.equal(await stranger.fails('POST', '/api/auth/register', 403, { json: form }), 'registration_closed');
    const { url } = await admin.post('/api/admin/invites', {});
    const invite = url.split('/').pop();
    await stranger.ok('POST', '/api/auth/register', { json: { ...form, invite } });
    await admin.patch('/api/admin/settings', { registration: true });
  });

  it('locks the site to everyone but admins', async () => {
    await admin.patch('/api/admin/settings', { locked: true, lockMessage: 'Back at noon.' });
    const me = await owner.get('/api/me');
    assert.deepEqual([me.settings.locked, me.settings.lockMessage], [true, 'Back at noon.']);
    const res = await owner.request('GET', '/api/documents?view=files');
    assert.equal(res.status, 503);
    assert.equal((await res.json()).message, 'Back at noon.');
    // Only admins sign in; they go on working.
    const again = server.client();
    assert.equal(await again.fails('POST', '/api/auth/login', 503, { json: { login: 'vic', password: 'password-123' } }), 'locked');
    await admin.ok('GET', '/api/documents?view=files');
    await admin.patch('/api/admin/settings', { locked: false });
    await owner.ok('GET', '/api/documents?view=files');
  });
});
