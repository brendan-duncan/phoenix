// Two-step sign-in (an authenticator app's codes, recovery codes), admins
// needing it for their admin powers, and being told of a sign-in from a new
// browser.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { codeAt, stepAt } from '../lib/totp.mjs';
import { PASSWORD, startServer } from './helpers.mjs';

let server;
let admin;
let ann;

before(async () => {
  server = await startServer({ env: { PHOENIX_CLOUD_TWO_STEP_IN_DEV: '1' } });
  admin = server.client();
  ann = server.client();
  await admin.signInWith('phoenix', 'animate');
  await ann.signIn('ann');
});

after(() => server.close());

/** Turns two-step sign-in on for `client`: its secret and recovery codes. */
async function turnOn(client) {
  const { secret, uri } = await client.post('/api/me/two-step/start');
  assert.match(uri, /^otpauth:\/\/totp\/Phoenix%20Cloud%3A/);
  assert.equal(await client.fails('POST', '/api/me/two-step/enable', 400, { json: { code: '000000' } }), 'bad_code');
  const { recoveryCodes } = await client.post('/api/me/two-step/enable', { code: codeAt(secret, stepAt()) });
  assert.equal(recoveryCodes.length, 10);
  return { secret, recoveryCodes };
}

describe('two-step sign-in', () => {
  let secret;
  let recoveryCodes;

  it('turns on with a code from the app, and then asks for one at sign-in', async () => {
    ({ secret, recoveryCodes } = await turnOn(ann));
    assert.deepEqual((await ann.get('/api/me')).twoStep, { on: true, recoveryCodesLeft: 10 });
    const fresh = server.client();
    const first = await fresh.ok('POST', '/api/auth/login', { json: { login: 'ann', password: PASSWORD } });
    assert.equal(first.twoStep, true);
    // No session yet.
    assert.equal((await fresh.get('/api/me')).user, null);
    assert.equal(await fresh.fails('POST', '/api/auth/two-step', 401, { json: { ticket: first.ticket, code: '123456' } }), 'bad_code');
    // The code just used to turn it on can't be used again; the next one can.
    await fresh.ok('POST', '/api/auth/two-step', { json: { ticket: first.ticket, code: codeAt(secret, stepAt() + 1) } });
    assert.equal((await fresh.get('/api/me')).user.name, 'ann');
  });

  it('takes each recovery code once', async () => {
    const fresh = server.client();
    const { ticket } = await fresh.ok('POST', '/api/auth/login', { json: { login: 'ann', password: PASSWORD } });
    const done = await fresh.ok('POST', '/api/auth/two-step', { json: { ticket, code: recoveryCodes[0].toUpperCase() } });
    assert.equal(done.recoveryCodesLeft, 9);
    const again = server.client();
    const second = await again.ok('POST', '/api/auth/login', { json: { login: 'ann', password: PASSWORD } });
    assert.equal(await again.fails('POST', '/api/auth/two-step', 401, { json: { ticket: second.ticket, code: recoveryCodes[0] } }), 'bad_code');
  });

  it('is not got around by a password-reset link', async () => {
    const token = (await import('../lib/accounts.mjs')).makeEmailToken(server.store, ann.user.id, 'reset', 2);
    const fresh = server.client();
    const reset = await fresh.ok('POST', '/api/auth/reset', { json: { token, password: 'a new long password' } });
    assert.equal(reset.twoStep, true);
    assert.equal((await fresh.get('/api/me')).user, null);
  });

  it('is turned off with a code, or by an admin for someone who lost their phone', async () => {
    const bo = server.client();
    await bo.signIn('bo');
    const { recoveryCodes: codes } = await turnOn(bo);
    await bo.ok('DELETE', '/api/me/two-step', { json: { code: codes[1] } });
    assert.equal((await bo.get('/api/me')).twoStep.on, false);
  });
});

describe('admins', () => {
  it('have no admin powers until they turn two-step sign-in on', async () => {
    assert.equal((await admin.get('/api/me')).adminNeedsTwoStep, true);
    assert.equal(await admin.fails('GET', '/api/admin/users', 403), 'two_step_needed');
    await turnOn(admin);
    assert.equal((await admin.get('/api/me')).adminNeedsTwoStep, false);
    await admin.get('/api/admin/users');
  });

  it('can turn it off for someone', async () => {
    const cy = server.client();
    await cy.signIn('cy');
    await turnOn(cy);
    await admin.patch(`/api/admin/users/${cy.user.id}`, { resetTwoStep: true });
    assert.equal(server.store.get("SELECT totp_secret FROM users WHERE handle = 'cy'").totp_secret, null);
    // Their sessions ended with it.
    assert.equal((await cy.get('/api/me')).user, null);
  });
});

describe('a sign-in from a new browser', () => {
  it('is told, once per browser, and not for the first one', async () => {
    const browser = server.client();
    await browser.signIn('dee');
    const told = () => server.store.get("SELECT COUNT(*) AS n FROM notifications n JOIN users u ON u.id = n.user_id WHERE u.handle = 'dee' AND n.kind = 'signin'").n;
    assert.equal(told(), 0);
    // The same browser again: nothing new.
    await browser.signInWith('dee', PASSWORD);
    assert.equal(told(), 0);
    // Another browser: told.
    await server.client().signInWith('dee', PASSWORD);
    assert.equal(told(), 1);
    assert.ok(server.app.mailer.recent().some((m) => m.to === 'dee@example.com' && m.subject === 'New sign-in to your Phoenix Cloud account'));
  });
});
