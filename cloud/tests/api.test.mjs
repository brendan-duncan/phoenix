// End to end through HTTP: several people signed in with dev login at once.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { makePhx, startServer } from './helpers.mjs';

let server;
let alice;
let bob;
let carol;
let anon;

before(async () => {
  server = await startServer();
  alice = server.client();
  bob = server.client();
  carol = server.client();
  anon = server.client();
  await alice.signIn('alice');
  await bob.signIn('bob');
  await carol.signIn('carol');
});

after(() => server.close());

describe('sessions', () => {
  it('makes the first dev user an admin', () => {
    assert.equal(alice.user.isAdmin, true);
    assert.equal(bob.user.isAdmin, false);
  });

  it('needs the CSRF token and our own origin for changes', async () => {
    const csrf = bob.csrf;
    bob.csrf = null;
    assert.equal(await bob.fails('POST', '/api/folders', 403, { json: { name: 'x' } }), 'csrf');
    bob.csrf = csrf;
    assert.equal(await bob.fails('POST', '/api/folders', 403, { json: { name: 'x' }, headers: { Origin: 'https://evil.example' } }), 'origin');
  });

  it('refuses changes from people who are signed out', async () => {
    assert.equal(await anon.fails('POST', '/api/folders', 401, { json: { name: 'x' } }), 'signed_out');
    assert.equal(await anon.fails('GET', '/api/documents', 401), 'signed_out');
  });

  it('lists and revokes sessions, and signs out', async () => {
    const dave = server.client();
    await dave.signIn('dave');
    const { sessions } = await dave.get('/api/me/sessions');
    assert.equal(sessions.length, 1);
    assert.equal(sessions[0].current, true);
    await dave.post('/api/auth/logout');
    const me = await dave.get('/api/me');
    assert.equal(me.user, null);
  });

  it('updates the profile and keeps handles unique', async () => {
    await carol.patch('/api/me', { name: 'Carol C', bio: 'hi' });
    assert.equal((await carol.get('/api/me')).user.name, 'Carol C');
    assert.equal(await carol.fails('PATCH', '/api/me', 409, { json: { handle: 'alice' } }), 'taken');
    assert.equal(await carol.fails('PATCH', '/api/me', 400, { json: { handle: '!' } }), 'bad_request');
  });
});

describe('documents', () => {
  let doc;
  let bytes;

  it('uploads a .phx and lists it with its thumbnail', async () => {
    bytes = await makePhx({ symbols: ['Hero', 'Tree'] });
    ({ document: doc } = await alice.upload('Scene 1.phx', bytes));
    assert.equal(doc.name, 'Scene 1');
    assert.equal(doc.revision, 1);
    assert.equal(doc.role, 'owner');
    assert.equal(doc.info.application, 'Phoenix 0.22.0');
    assert.ok(doc.thumbnailUrl);
    const list = await alice.get('/api/documents?view=files');
    assert.deepEqual(list.documents.map((d) => d.id), [doc.id]);
    assert.ok(list.usage.bytes > 0);
    const thumb = await alice.request('GET', doc.thumbnailUrl);
    assert.equal(thumb.headers.get('content-type'), 'image/png');
  });

  it('gives back the same bytes', async () => {
    const res = await alice.request('GET', `/api/documents/${doc.id}/file`);
    assert.equal(res.status, 200);
    assert.ok(Buffer.from(await res.arrayBuffer()).equals(bytes));
    assert.match(res.headers.get('content-disposition'), /Scene 1\.phx/);
  });

  it('refuses files that are not .phx', async () => {
    assert.equal(await alice.fails('POST', '/api/documents/upload?name=x.phx', 422, { body: Buffer.from('nope'), headers: { 'Content-Type': 'application/octet-stream' } }), 'not_phx');
  });

  it('keeps revisions, refuses a stale save and restores', async () => {
    const v2 = await makePhx({ symbols: ['Hero', 'Tree', 'Cloud'] });
    const put = (b, base) => alice.ok('PUT', `/api/documents/${doc.id}/file?base=${base}`, { body: b, headers: { 'Content-Type': 'application/octet-stream' } });
    assert.equal((await put(v2, 1)).revision, 2);
    assert.equal(await alice.fails('PUT', `/api/documents/${doc.id}/file?base=1`, 409, { body: v2, headers: { 'Content-Type': 'application/octet-stream' } }), 'conflict');
    const { revisions } = await alice.get(`/api/documents/${doc.id}/revisions`);
    assert.deepEqual(revisions.map((r) => r.number), [2, 1]);
    assert.equal((await alice.post(`/api/documents/${doc.id}/revisions/1/restore`)).revision, 3);
    const res = await alice.request('GET', `/api/documents/${doc.id}/file`);
    assert.ok(Buffer.from(await res.arrayBuffer()).equals(bytes));
    // Revisions share chunks: three revisions store far less than three files.
    const stored = server.store.get('SELECT SUM(size) AS n FROM blobs').n;
    assert.ok(stored < bytes.length + v2.length);
  });

  it('is invisible to others until shared', async () => {
    assert.equal(await bob.fails('GET', `/api/documents/${doc.id}`, 404), 'not_found');
    assert.equal(await anon.fails('GET', `/api/documents/${doc.id}`, 401), 'signed_out');
  });

  it('shares with people at a role', async () => {
    await alice.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'bob', role: 'viewer' });
    assert.equal((await bob.get(`/api/documents/${doc.id}`)).document.role, 'viewer');
    assert.deepEqual((await bob.get('/api/documents?view=shared')).documents.map((d) => d.id), [doc.id]);
    assert.equal(await bob.fails('PUT', `/api/documents/${doc.id}/file`, 403, { body: bytes, headers: { 'Content-Type': 'application/octet-stream' } }), 'forbidden');
    await alice.patch(`/api/documents/${doc.id}/shares/user/${bob.user.id}`, { role: 'editor' });
    await bob.ok('PUT', `/api/documents/${doc.id}/file`, { body: bytes, headers: { 'Content-Type': 'application/octet-stream' } });
    // Editors can't change who sees it.
    assert.equal(await bob.fails('PATCH', `/api/documents/${doc.id}`, 403, { json: { visibility: 'public' } }), 'forbidden');
    assert.equal(await bob.fails('POST', `/api/documents/${doc.id}/shares`, 403, { json: { kind: 'user', who: 'carol', role: 'viewer' } }), 'forbidden');
  });

  it('tells the sharer when the person is not a member yet', async () => {
    const res = await alice.request('POST', `/api/documents/${doc.id}/shares`, { json: { kind: 'user', who: 'stranger@example.com', role: 'viewer' } });
    assert.equal(res.status, 404);
    assert.match((await res.json()).message, /invite them/);
  });

  it('lets a viewer leave a share', async () => {
    await alice.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'carol', role: 'viewer' });
    await carol.del(`/api/documents/${doc.id}/shares/user/${carol.user.id}`);
    assert.equal(await carol.fails('GET', `/api/documents/${doc.id}`, 404), 'not_found');
  });

  it('respects the download and duplicate switches', async () => {
    await alice.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'carol', role: 'viewer' });
    await alice.patch(`/api/documents/${doc.id}`, { allowDownload: false, allowDuplicate: false });
    assert.equal(await carol.fails('GET', `/api/documents/${doc.id}/file`, 403), 'forbidden');
    assert.equal(await carol.fails('POST', `/api/documents/${doc.id}/duplicate`, 403, { json: {} }), 'forbidden');
    await alice.patch(`/api/documents/${doc.id}`, { allowDownload: true, allowDuplicate: true });
    const { document: copy } = await carol.post(`/api/documents/${doc.id}/duplicate`);
    assert.equal(copy.owner.id, carol.user.id);
    assert.equal(copy.forkedFrom, doc.id);
    assert.equal(copy.revision, 1);
    const res = await carol.request('GET', `/api/documents/${copy.id}/file`);
    assert.equal(res.status, 200);
  });

  it('opens by link only for members during the beta', async () => {
    const { url } = await alice.post(`/api/documents/${doc.id}/links`, { role: 'viewer' });
    const token = new URL(url).searchParams.get('link');
    const dave = server.client();
    await dave.signIn('dave2');
    dave.link = token;
    assert.equal((await dave.get(`/api/documents/${doc.id}`)).document.role, 'viewer');
    anon.link = token;
    assert.equal(await anon.fails('GET', `/api/documents/${doc.id}`, 401), 'signed_out');
    anon.link = null;
    server.store.saveSettings({ beta: false });
    try {
      anon.link = token;
      assert.equal((await anon.get(`/api/documents/${doc.id}`)).document.role, 'viewer');
    } finally {
      anon.link = null;
      server.store.saveSettings({ beta: true });
    }
    const { links } = await alice.get(`/api/documents/${doc.id}/shares`);
    await alice.del(`/api/documents/${doc.id}/links/${links[0].id}`);
    assert.equal(await dave.fails('GET', `/api/documents/${doc.id}`, 404), 'not_found');
  });

  it('shows public documents to members only during the beta', async () => {
    const { document: pub } = await alice.upload('Public piece', await makePhx({ document: 'pub' }));
    await alice.patch(`/api/documents/${pub.id}`, { visibility: 'public' });
    assert.equal((await carol.get(`/api/documents/${pub.id}`)).document.role, 'viewer');
    assert.equal(await anon.fails('GET', `/api/documents/${pub.id}`, 401), 'signed_out');
  });

  it('trashes, restores and purges', async () => {
    const { document: d } = await alice.upload('Doomed', await makePhx({ document: 'doomed' }));
    await alice.post(`/api/documents/${d.id}/shares`, { kind: 'user', who: 'bob', role: 'editor' });
    await alice.del(`/api/documents/${d.id}`);
    assert.equal(await bob.fails('GET', `/api/documents/${d.id}`, 404), 'not_found');
    assert.ok((await alice.get('/api/documents?view=trash')).documents.some((x) => x.id === d.id));
    await alice.post(`/api/documents/${d.id}/restore`);
    assert.equal((await bob.get(`/api/documents/${d.id}`)).document.role, 'editor');
    assert.equal(await alice.fails('DELETE', `/api/documents/${d.id}?purge=1`, 400), 'bad_request');
    await alice.del(`/api/documents/${d.id}`);
    await alice.del(`/api/documents/${d.id}?purge=1`);
    assert.equal(await alice.fails('GET', `/api/documents/${d.id}`, 404), 'not_found');
  });

  it('keeps favorites and recents, and finds by name', async () => {
    await bob.ok('PUT', `/api/favorites/document/${doc.id}`);
    assert.deepEqual((await bob.get('/api/documents?view=favorites')).documents.map((d) => d.id), [doc.id]);
    await bob.post(`/api/documents/${doc.id}/opened`);
    assert.equal((await bob.get('/api/documents?view=recent')).documents[0].id, doc.id);
    assert.deepEqual((await bob.get('/api/documents?q=scene')).documents.map((d) => d.id), [doc.id]);
    assert.deepEqual((await carol.get('/api/documents?q=%25')).documents.length, 0);
  });

  it('enforces the storage quota', async () => {
    const erin = server.client();
    const me = await erin.signIn('erin');
    server.store.run('UPDATE users SET quota_mb = 0 WHERE id = ?', me.id);
    assert.equal(await erin.fails('POST', '/api/documents/upload?name=big', 413, { body: await makePhx({ document: 'big' }), headers: { 'Content-Type': 'application/octet-stream' } }), 'quota');
  });
});

describe('folders', () => {
  it('nests, moves documents and trashes contents when deleted', async () => {
    const { folder: outer } = await bob.post('/api/folders', { name: 'Outer' });
    const { folder: inner } = await bob.post('/api/folders', { name: 'Inner', parentId: outer.id });
    const { document: d } = await bob.upload('In inner', await makePhx({ document: 'inner' }), `&folderId=${inner.id}`);
    const listing = await bob.get(`/api/documents?view=files&folder=${inner.id}`);
    assert.deepEqual(listing.path.map((f) => f.name), ['Outer', 'Inner']);
    const { folders } = await bob.get('/api/folders');
    assert.deepEqual(folders.map((f) => f.name).sort(), ['Inner', 'Outer']);
    assert.equal(await carol.fails('GET', '/api/folders?group=nope', 404), 'not_found');
    assert.deepEqual(listing.documents.map((x) => x.id), [d.id]);
    assert.equal(await bob.fails('PATCH', `/api/folders/${outer.id}`, 400, { json: { parentId: inner.id } }), 'bad_request');
    assert.equal(await carol.fails('PATCH', `/api/folders/${outer.id}`, 404, { json: { name: 'mine' } }), 'not_found');
    await bob.del(`/api/folders/${outer.id}`);
    const trash = await bob.get('/api/documents?view=trash');
    assert.ok(trash.documents.some((x) => x.id === d.id));
    await bob.post(`/api/documents/${d.id}/restore`);
    assert.equal((await bob.get(`/api/documents/${d.id}`)).document.folderId, null);
  });
});

describe('groups', () => {
  let group;

  it('makes its creator the first admin', async () => {
    ({ group } = await alice.post('/api/groups', { name: 'Night Shift' }));
    assert.equal(group.handle, 'nightshift');
    const detail = await alice.get(`/api/groups/${group.id}`);
    assert.equal(detail.role, 'admin');
    assert.equal(detail.members.length, 1);
  });

  it('lets admins add members, who then reach the group\'s files', async () => {
    assert.equal(await bob.fails('GET', `/api/groups/${group.id}`, 404), 'not_found');
    await alice.post(`/api/groups/${group.id}/members`, { who: 'bob' });
    const { document: d } = await alice.upload('Group scene', await makePhx({ document: 'group' }), `&groupId=${group.id}`);
    assert.equal(d.owner.kind, 'group');
    assert.equal((await bob.get(`/api/documents/${d.id}`)).document.role, 'editor');
    assert.deepEqual((await bob.get(`/api/documents?view=group&group=${group.id}`)).documents.map((x) => x.id), [d.id]);
    await alice.patch(`/api/groups/${group.id}`, { defaultRole: 'viewer' });
    assert.equal((await bob.get(`/api/documents/${d.id}`)).document.role, 'viewer');
    assert.equal(await bob.fails('POST', `/api/groups/${group.id}/members`, 403, { json: { who: 'carol' } }), 'forbidden');
    // Only admins get the owner role on group files, and can trash them.
    assert.equal(await bob.fails('DELETE', `/api/documents/${d.id}`, 403), 'forbidden');
  });

  it('shares a personal document with the group', async () => {
    const { document: d } = await carol.upload('For the group', await makePhx({ document: 'carol' }));
    assert.equal(await carol.fails('POST', `/api/documents/${d.id}/shares`, 403, { json: { kind: 'group', who: group.id, role: 'viewer' } }), 'forbidden');
    await alice.post(`/api/groups/${group.id}/members`, { who: 'carol' });
    await carol.post(`/api/documents/${d.id}/shares`, { kind: 'group', who: group.handle, role: 'commenter' });
    assert.equal((await bob.get(`/api/documents/${d.id}`)).document.role, 'commenter');
  });

  it('keeps at least one admin', async () => {
    assert.equal(await alice.fails('DELETE', `/api/groups/${group.id}/members/${alice.user.id}`, 409), 'last_admin');
    assert.equal(await alice.fails('PATCH', `/api/groups/${group.id}/members/${alice.user.id}`, 409, { json: { role: 'member' } }), 'last_admin');
    await alice.patch(`/api/groups/${group.id}/members/${bob.user.id}`, { role: 'admin' });
    await alice.del(`/api/groups/${group.id}/members/${alice.user.id}`);
    // She has left (as a site admin she could still look at it).
    assert.ok(!(await alice.get('/api/groups')).groups.some((g) => g.id === group.id));
    assert.equal((await alice.get(`/api/groups/${group.id}`)).role, null);
  });

  it('hands its files to the admin who deletes it', async () => {
    const before = await bob.get(`/api/documents?view=group&group=${group.id}`);
    await bob.del(`/api/groups/${group.id}`);
    const mine = await bob.get('/api/documents?view=files');
    const folder = mine.folders.find((f) => f.name === 'Night Shift');
    assert.ok(folder);
    const inside = await bob.get(`/api/documents?view=files&folder=${folder.id}`);
    assert.deepEqual(inside.documents.map((d) => d.id), before.documents.map((d) => d.id));
  });
});

describe('admin', () => {
  it('is for admins only', async () => {
    assert.equal(await bob.fails('GET', '/api/admin/users', 403), 'forbidden');
  });

  it('makes invites and lists them', async () => {
    const { url, invite } = await alice.post('/api/admin/invites', { email: 'new@example.com', note: 'Friend' });
    assert.match(url, /\/invite\/[A-Za-z0-9_-]{43}$/);
    assert.equal(invite.status, 'pending');
    const token = url.split('/').pop();
    const check = await anon.get(`/api/invites/${token}`);
    assert.equal(check.valid, true);
    assert.equal(check.email, 'n•••@example.com');
    await alice.del(`/api/admin/invites/${invite.id}`);
    assert.equal((await anon.get(`/api/invites/${token}`)).valid, false);
  });

  it('suspends users, which ends their sessions', async () => {
    const frank = server.client();
    const me = await frank.signIn('frank');
    await alice.patch(`/api/admin/users/${me.id}`, { status: 'suspended' });
    assert.equal((await frank.get('/api/me')).user, null);
    assert.equal(await alice.fails('PATCH', `/api/admin/users/${alice.user.id}`, 403, { json: { isAdmin: false } }), 'forbidden');
  });

  it('changes settings within bounds and logs it', async () => {
    assert.equal(await alice.fails('PATCH', '/api/admin/settings', 400, { json: { signupPolicy: 'everyone' } }), 'bad_request');
    const { settings } = await alice.patch('/api/admin/settings', { banner: 'Hello beta' });
    assert.equal(settings.banner, 'Hello beta');
    const { entries } = await alice.get('/api/admin/audit');
    assert.equal(entries[0].action, 'settings.change');
    const stats = await alice.get('/api/admin/stats');
    assert.ok(stats.documents > 0 && stats.blobs.bytes > 0);
  });
});

describe('pages', () => {
  it('serves the dashboard with its security headers, and no index during the beta', async () => {
    const res = await anon.request('GET', '/files');
    assert.equal(res.status, 200);
    assert.match(res.headers.get('content-security-policy'), /default-src 'self'/);
    assert.equal(res.headers.get('x-robots-tag'), 'noindex, nofollow');
    assert.match(await (await anon.request('GET', '/robots.txt')).text(), /Disallow: \/$/m);
  });

  it('explains a missing editor build', async () => {
    const res = await anon.request('GET', '/editor/');
    assert.equal(res.status, 404);
    assert.match(await res.text(), /build_web\.py/);
  });
});
