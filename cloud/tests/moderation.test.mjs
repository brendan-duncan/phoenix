// Admins looking at people's accounts and documents without leaving a trace
// in them, and moderation: reports, flagging and removing documents, and
// clearing an offensive profile.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { makePhx, startServer } from './helpers.mjs';

let server;
let admin;
let ann;
let bo;
let doc;

const octets = { 'Content-Type': 'application/octet-stream' };

before(async () => {
  server = await startServer();
  admin = server.client();
  ann = server.client();
  bo = server.client();
  await admin.signInWith('phoenix', 'animate');
  await ann.signIn('ann');
  await bo.signIn('bo');
  ({ document: doc } = await ann.upload('Private sketch.phx', await makePhx({ symbols: ['A'] })));
});

after(() => server.close());

describe('an admin\'s look', () => {
  it('sees anyone\'s documents, passively', async () => {
    assert.equal(await bo.fails('GET', `/api/documents/${doc.id}`, 404), 'not_found');
    const seen = (await admin.get(`/api/documents/${doc.id}`)).document;
    assert.deepEqual([seen.role, seen.adminView, seen.canOpen, seen.canDownload], ['viewer', true, true, false]);
    // Not a view: not in their recents.
    await admin.post(`/api/documents/${doc.id}/opened`);
    assert.ok(!(await admin.get('/api/documents?view=recent')).documents.some((d) => d.id === doc.id));
    // The editor opens it, but takes no lock and keeps no draft; Ann goes on editing.
    assert.equal((await admin.request('GET', `/api/documents/${doc.id}/file`)).status, 200);
    assert.equal(await admin.fails('POST', `/api/documents/${doc.id}/lock`, 403, { json: {} }), 'forbidden');
    assert.equal(await admin.fails('PUT', `/api/documents/${doc.id}/draft`, 403, { body: Buffer.from('x'), headers: octets }), 'forbidden');
    assert.equal(await admin.fails('GET', `/api/documents/${doc.id}/download`, 403), 'forbidden');
    assert.equal((await ann.post(`/api/documents/${doc.id}/lock`)).held, true);
    // Each admin's look is in the activity log, once an hour.
    await admin.get(`/api/documents/${doc.id}`);
    const looks = server.store.all("SELECT * FROM audit_log WHERE action = 'document.admin_view' AND target_id = ?", doc.id);
    assert.equal(looks.length, 1);
  });

  it('lists an account\'s documents and details, for admins only', async () => {
    assert.deepEqual((await admin.get(`/api/documents?view=user&user=${ann.user.id}`)).documents.map((d) => [d.name, d.adminView]), [['Private sketch', true]]);
    assert.equal(await bo.fails('GET', `/api/documents?view=user&user=${ann.user.id}`, 403), 'forbidden');
    const detail = await admin.get(`/api/admin/users/${ann.user.id}`);
    assert.equal(detail.user.name, 'ann');
    assert.equal(detail.counts.moderated, 0);
  });
});

describe('moderation', () => {
  let link;

  it('takes reports from people who can see a document', async () => {
    await ann.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'bo', role: 'viewer' });
    ({ url: link } = await ann.post(`/api/documents/${doc.id}/links`, { role: 'viewer' }));
    assert.equal(await ann.fails('POST', `/api/documents/${doc.id}/report`, 400, { json: {} }), 'bad_request');
    await bo.post(`/api/documents/${doc.id}/report`, { reason: 'Offensive' });
    const { reports } = await admin.get('/api/admin/document-reports');
    assert.deepEqual(reports.map((r) => [r.reporter.name, r.reason, r.document.name]), [['bo', 'Offensive', 'Private sketch']]);
  });

  it('flags: no more links or public view, and tells the owner', async () => {
    const { reports } = await admin.get('/api/admin/document-reports');
    await admin.patch(`/api/admin/document-reports/${reports[0].id}`, { outcome: 'flagged', note: 'Needs a content warning' });
    assert.equal((await admin.get('/api/admin/document-reports')).reports.length, 0);
    const token = new URL(link).searchParams.get('link');
    const carl = server.client();
    await carl.signIn('carl');
    assert.equal(await carl.fails('GET', `/api/documents/${doc.id}?link=${token}`, 404), 'not_found');
    // People it is shared with still see it; its owner sees why.
    assert.equal((await bo.get(`/api/documents/${doc.id}`)).document.moderation, null);
    assert.deepEqual((await ann.get(`/api/documents/${doc.id}`)).document.moderation.state, 'flagged');
    assert.ok((await ann.get('/api/notifications')).notifications.some((n) => n.kind === 'moderation' && n.text.includes('Needs a content warning')));
  });

  it('removes: the owner keeps only the record of it, and can delete it', async () => {
    await admin.patch(`/api/admin/documents/${doc.id}/moderation`, { state: 'removed', note: 'Hateful imagery' });
    assert.equal(await bo.fails('GET', `/api/documents/${doc.id}`, 404), 'not_found');
    const mine = (await ann.get(`/api/documents/${doc.id}`)).document;
    assert.deepEqual([mine.moderation.state, mine.canOpen, mine.canDownload], ['removed', false, false]);
    assert.equal(await ann.fails('GET', `/api/documents/${doc.id}/file`, 403), 'removed');
    assert.equal(await ann.fails('POST', `/api/documents/${doc.id}/shares`, 403, { json: { kind: 'user', who: 'carl', role: 'viewer' } }), 'removed');
    // Admins still see it all.
    assert.equal((await admin.request('GET', `/api/documents/${doc.id}/file`)).status, 200);
    await admin.patch(`/api/admin/documents/${doc.id}/moderation`, { state: null });
    assert.equal((await ann.request('GET', `/api/documents/${doc.id}/file`)).status, 200);
    assert.ok((await ann.get('/api/notifications')).notifications.some((n) => n.text.startsWith('The moderators restored')));
  });

  it('clears an offensive profile', async () => {
    await bo.patch('/api/me', { bio: 'Something offensive' });
    await admin.patch(`/api/admin/users/${bo.user.id}`, { clearBio: true, clearAvatar: true });
    assert.equal((await admin.get(`/api/admin/users/${bo.user.id}`)).user.bio, '');
  });
});
