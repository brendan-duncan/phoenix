// Account plans, teams' plans, handing documents and groups on, and people
// deleting their own accounts.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { newId } from '../lib/ids.mjs';
import { effectivePlan } from '../lib/plans.mjs';
import { makePhx, PASSWORD, startServer } from './helpers.mjs';

let server;
let admin;
let ann;
let bo;
let cy;
let bytes;

before(async () => {
  server = await startServer();
  admin = server.client();
  ann = server.client();
  bo = server.client();
  cy = server.client();
  await admin.signInWith('phoenix', 'animate');
  await ann.signIn('ann');
  await bo.signIn('bo');
  await cy.signIn('cy');
  bytes = await makePhx({ symbols: ['One'] });
});

after(() => server.close());

const user = (name) => server.store.get('SELECT * FROM users WHERE handle = ?', name);

describe('plans', () => {
  it('start as the setting says, and an admin changes them', async () => {
    assert.deepEqual((await ann.get('/api/me')).plan, { id: 'beta', label: 'Beta', team: null, managed: false });
    await admin.patch(`/api/admin/users/${ann.user.id}`, { plan: 'free' });
    assert.equal((await ann.get('/api/me')).plan.id, 'free');
    // Pro is not offered yet.
    assert.equal(await admin.fails('PATCH', `/api/admin/users/${ann.user.id}`, 400, { json: { plan: 'pro' } }), 'bad_request');
    await admin.patch('/api/admin/settings', { defaultPlan: 'free' });
    assert.equal(server.store.settings.defaultPlan, 'free');
    await admin.patch('/api/admin/settings', { defaultPlan: 'beta' });
  });

  it('come from a team: the better of the two, or the team\'s when it manages the account', () => {
    const { store } = server;
    const team = newId();
    store.run("INSERT INTO teams (id, name, owner_id, plan, created_at) VALUES (?, 'Studio', ?, 'enterprise', ?)", team, bo.user.id, Date.now());
    store.run('INSERT INTO team_members (team_id, user_id, role, managed, added_at) VALUES (?, ?, \'member\', 0, ?)', team, ann.user.id, Date.now());
    assert.deepEqual(effectivePlan(store, user('ann')), { plan: 'enterprise', team: { id: team, name: 'Studio' }, managed: false });
    store.run("UPDATE teams SET plan = 'free' WHERE id = ?", team);
    store.run("UPDATE users SET plan = 'beta' WHERE id = ?", ann.user.id);
    assert.equal(effectivePlan(store, user('ann')).plan, 'beta');
    store.run('UPDATE team_members SET managed = 1 WHERE team_id = ?', team);
    assert.deepEqual(effectivePlan(store, user('ann')), { plan: 'free', team: { id: team, name: 'Studio' }, managed: true });
  });

  it('a managed account is the team\'s to delete', async () => {
    const choices = await ann.get('/api/me/deletion');
    assert.deepEqual(choices.managedBy?.name, 'Studio');
    assert.equal(await ann.fails('DELETE', '/api/me', 403, { json: { password: PASSWORD } }), 'managed');
    server.store.run('DELETE FROM teams');
  });
});

describe('handing documents on', () => {
  it('to a person, who owns it; the owner before goes on as an editor', async () => {
    const folder = await ann.post('/api/folders', { name: 'Shots' });
    const { document: doc } = await ann.upload('Opening.phx', bytes, `&folderId=${folder.folder.id}`);
    await ann.post(`/api/documents/${doc.id}/transfer`, { kind: 'user', to: 'bo' });
    const mine = (await bo.get(`/api/documents/${doc.id}`)).document;
    assert.deepEqual([mine.role, mine.owner.id, mine.folderId], ['owner', bo.user.id, null]);
    const theirs = (await ann.get(`/api/documents/${doc.id}`)).document;
    assert.deepEqual([theirs.role, theirs.canDownload], ['editor', true]);
    // Only the owner hands it on.
    assert.equal(await ann.fails('POST', `/api/documents/${doc.id}/transfer`, 403, { json: { kind: 'user', to: 'cy' } }), 'forbidden');
  });

  it('to a group they are in', async () => {
    const { group } = await ann.post('/api/groups', { name: 'Layout dept' });
    const { document: doc } = await ann.upload('Board.phx', bytes);
    assert.equal(await bo.fails('POST', `/api/documents/${doc.id}/transfer`, 404, { json: { kind: 'group', to: group.id } }), 'not_found');
    await ann.post(`/api/documents/${doc.id}/transfer`, { kind: 'group', to: group.id });
    const moved = (await ann.get(`/api/documents/${doc.id}`)).document;
    assert.deepEqual([moved.owner.kind, moved.owner.id], ['group', group.id]);
  });
});

describe('group owners', () => {
  it('own what they make, and hand it to another member', async () => {
    const { group } = await bo.post('/api/groups', { name: 'Night crew' });
    assert.equal(group.ownerId, bo.user.id);
    await bo.post(`/api/groups/${group.id}/members`, { who: 'cy' });
    assert.equal(await bo.fails('DELETE', `/api/groups/${group.id}/members/${bo.user.id}`, 409), 'owner');
    assert.equal(await bo.fails('POST', `/api/groups/${group.id}/transfer`, 400, { json: { to: 'ann' } }), 'bad_request');
    const after = await bo.post(`/api/groups/${group.id}/transfer`, { to: 'cy' });
    assert.equal(after.group.ownerId, cy.user.id);
    assert.equal(after.members.find((m) => m.user.id === cy.user.id).role, 'admin');
    // The owner before is an admin still, but can no longer delete it.
    assert.equal(await bo.fails('DELETE', `/api/groups/${group.id}`, 403), 'forbidden');
    await bo.del(`/api/groups/${group.id}/members/${bo.user.id}`);
  });
});

describe('deleting an account', () => {
  it('hands on what others use, and deletes the rest', async () => {
    const dee = server.client();
    await dee.signIn('dee');
    const { document: shared } = await dee.upload('Shared.phx', bytes);
    const { document: alone } = await dee.upload('Alone.phx', bytes);
    await dee.post(`/api/documents/${shared.id}/shares`, { kind: 'user', who: 'ann', role: 'viewer' });
    const { group } = await dee.post('/api/groups', { name: 'Dee crew' });
    await dee.post(`/api/groups/${group.id}/members`, { who: 'bo' });
    const { group: solo } = await dee.post('/api/groups', { name: 'Dee solo' });

    const choices = await dee.get('/api/me/deletion');
    assert.deepEqual(choices.documents.map((d) => [d.name, d.candidates.map((c) => c.name)]), [['Shared', ['ann']]]);
    assert.deepEqual(choices.groups.map((g) => [g.name, g.candidates.map((c) => c.name)]), [['Dee crew', ['bo']], ['Dee solo', []]]);
    assert.equal(choices.documentCount, 2);

    assert.equal(await dee.fails('DELETE', '/api/me', 403, { json: { password: 'not it' } }), 'confirm');
    // A group with other members needs someone to take it over.
    assert.equal(await dee.fails('DELETE', '/api/me', 409, { json: { password: PASSWORD } }), 'group_transfer');
    await dee.ok('DELETE', '/api/me', { json: { password: PASSWORD, documents: { [shared.id]: ann.user.id }, groups: { [group.id]: bo.user.id } } });

    assert.equal(user('dee'), undefined);
    assert.equal((await ann.get(`/api/documents/${shared.id}`)).document.role, 'owner');
    assert.equal(server.store.get('SELECT 1 FROM documents WHERE id = ?', alone.id), undefined);
    assert.equal(server.store.get('SELECT owner_id FROM groups WHERE id = ?', group.id).owner_id, bo.user.id);
    assert.equal(server.store.get('SELECT 1 FROM groups WHERE id = ?', solo.id), undefined);
    assert.equal(await dee.fails('GET', '/api/documents?view=files', 401), 'signed_out');
  });

  it('is refused to the only admin', async () => {
    assert.equal((await admin.get('/api/me/deletion')).lastAdmin, true);
    assert.equal(await admin.fails('DELETE', '/api/me', 409, { json: { password: 'animate' } }), 'last_admin');
  });
});
