// Direct messages and who may send them, groups' chats, blocks, reports,
// and each person's choice of notifications.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { makePhx, startServer } from './helpers.mjs';

let server;
let admin;
let ann;
let bo;
let cy;
let group;

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
  ({ group } = await ann.post('/api/groups', { name: 'Storyboard' }));
  await ann.post(`/api/groups/${group.id}/members`, { who: 'bo' });
});

after(() => server.close());

const send = (client, id, body) => client.post(`/api/conversations/${id}/messages`, { body });

describe('direct messages', () => {
  let dm;

  it('reach people in a group with the sender, by default', async () => {
    // Bo shares a group with Ann; Cy does not.
    ({ conversation: dm } = await bo.post('/api/conversations', { to: 'ann' }));
    assert.equal(dm.title, 'ann');
    assert.equal(await cy.fails('POST', '/api/conversations', 403, { json: { to: 'ann' } }), 'not_taking');
    assert.match((await cy.get('/api/messaging/check?to=ann')).why, /people in a group with them/);
    await send(bo, dm.id, 'Morning! Boards are up.');
    assert.equal((await ann.get('/api/me')).unreadMessages, 1);
    const { messages } = await ann.get(`/api/conversations/${dm.id}/messages`);
    assert.deepEqual(messages.map((m) => [m.sender.name, m.body]), [['bo', 'Morning! Boards are up.']]);
    await ann.post(`/api/conversations/${dm.id}/read`, {});
    assert.equal((await ann.get('/api/me')).unreadMessages, 0);
    // Told once, as a notification.
    assert.ok((await ann.get('/api/notifications')).notifications.some((n) => n.kind === 'message' && n.link === `/messages/${dm.id}`));
  });

  it('follow each person\'s choice, and the people they chose', async () => {
    await ann.patch('/api/me/messaging', { policy: 'none' });
    assert.equal(await bo.fails('POST', `/api/conversations/${dm.id}/messages`, 403, { json: { body: 'Still there?' } }), 'not_taking');
    await ann.patch('/api/me/messaging', { policy: 'chosen' });
    await ann.ok('PUT', '/api/me/allowed/cy');
    const { conversation } = await cy.post('/api/conversations', { to: 'ann' });
    await send(cy, conversation.id, 'Hi Ann');
    // Once Ann writes to Bo, Bo may answer, whatever her choice.
    await send(ann, dm.id, 'Thanks!');
    await send(bo, dm.id, 'Any time.');
    await ann.patch('/api/me/messaging', { policy: 'everyone' });
    assert.equal(await ann.fails('PATCH', '/api/me/messaging', 400, { json: { policy: 'friends' } }), 'bad_request');
  });

  it('stop both ways when one blocks the other', async () => {
    await ann.ok('PUT', '/api/me/blocks/bo');
    assert.equal(await bo.fails('POST', `/api/conversations/${dm.id}/messages`, 403, { json: { body: 'Hello?' } }), 'not_taking');
    assert.equal(await ann.fails('POST', `/api/conversations/${dm.id}/messages`, 403, { json: { body: 'Hello?' } }), 'not_taking');
    assert.deepEqual((await ann.get('/api/me/messaging')).blocked.map((u) => u.name), ['bo']);
    assert.equal((await ann.get(`/api/conversations/${dm.id}`)).conversation.blocked, true);
    await ann.del('/api/me/blocks/bo');
  });
});

describe('group chat', () => {
  it('is for the group\'s members, and hides people one blocked', async () => {
    const { conversation } = await ann.get(`/api/groups/${group.id}/chat`);
    assert.equal(conversation.kind, 'group');
    assert.equal(await cy.fails('GET', `/api/conversations/${conversation.id}/messages`, 404), 'not_found');
    await send(ann, conversation.id, 'Kickoff at ten');
    await send(bo, conversation.id, 'See you there');
    assert.ok((await bo.get('/api/conversations')).conversations.some((c) => c.id === conversation.id));
    await ann.ok('PUT', '/api/me/blocks/bo');
    assert.deepEqual((await ann.get(`/api/conversations/${conversation.id}/messages`)).messages.map((m) => m.body), ['Kickoff at ten']);
    await ann.del('/api/me/blocks/bo');
  });
});

describe('deleting and reporting', () => {
  it('lets senders delete theirs, and admins settle reports', async () => {
    const { conversation } = await ann.get(`/api/groups/${group.id}/chat`);
    const { message: rude } = await send(bo, conversation.id, 'Something rude');
    const { message: oops } = await send(ann, conversation.id, 'Oops, wrong chat');
    assert.equal(await ann.fails('DELETE', `/api/messages/${rude.id}`, 403), 'forbidden');
    await ann.del(`/api/messages/${oops.id}`);
    assert.ok((await bo.get(`/api/conversations/${conversation.id}/messages`)).messages.find((m) => m.id === oops.id).deleted);
    assert.equal(await cy.fails('POST', `/api/messages/${rude.id}/report`, 404, { json: {} }), 'not_found');
    await ann.post(`/api/messages/${rude.id}/report`, { reason: 'Not kind' });
    const { reports } = await admin.get('/api/admin/reports');
    assert.deepEqual(reports.map((r) => [r.reporter.name, r.message.body, r.reason]), [['ann', 'Something rude', 'Not kind']]);
    assert.ok((await admin.get('/api/notifications')).notifications.some((n) => n.kind === 'report'));
    await admin.patch(`/api/admin/reports/${reports[0].id}`, { outcome: 'removed' });
    assert.equal((await admin.get('/api/admin/reports')).reports.length, 0);
    assert.ok((await ann.get(`/api/conversations/${conversation.id}/messages`)).messages.find((m) => m.id === rude.id).deleted);
  });
});

describe('notification choices', () => {
  it('turn kinds off in the dashboard and on by email', async () => {
    const { kinds } = await cy.get('/api/me/notify');
    assert.ok(!kinds.some((k) => k.kind === 'waiting'), 'admins only');
    assert.deepEqual(kinds.find((k) => k.kind === 'share'), { kind: 'share', label: 'A document is shared with you', always: false, inApp: true, email: false });
    await cy.patch('/api/me/notify', { share: { inApp: false, email: true }, unlocked: { inApp: false } });
    const after = (await cy.get('/api/me/notify')).kinds;
    assert.deepEqual(after.find((k) => k.kind === 'share'), { kind: 'share', label: 'A document is shared with you', always: false, inApp: false, email: true });
    assert.equal(after.find((k) => k.kind === 'unlocked').inApp, true, 'always on');

    const { document: doc } = await ann.upload('Board.phx', await makePhx({ symbols: ['A'] }));
    await ann.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'cy', role: 'viewer' });
    assert.ok(!(await cy.get('/api/notifications')).notifications.some((n) => n.kind === 'share'));
    assert.ok(server.app.mailer.recent().some((m) => m.to === 'cy@example.com' && m.subject === 'ann shared “Board” with you'));
  });

  it('can be cleared', async () => {
    await bo.del('/api/notifications');
    assert.equal((await bo.get('/api/notifications')).notifications.length, 0);
  });
});
