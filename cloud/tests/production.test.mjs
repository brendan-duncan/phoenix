// What keeps a production server safe when its source is public: the first
// admin's password is not the one in the README, and the service won't run
// without knowing its own https address.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { after, describe, it } from 'node:test';
import { DEFAULT_ADMIN, ensureDefaultAdmin, findAccount } from '../lib/accounts.mjs';
import { loadConfig } from '../lib/config.mjs';
import { verifyPassword } from '../lib/passwords.mjs';
import { Store } from '../lib/store.mjs';

const made = [];
function freshStore() {
  const dir = fs.mkdtempSync(path.join(process.env.PHOENIX_TEST_TEMP ?? os.tmpdir(), 'phoenix-prod-'));
  const store = new Store(path.join(dir, 'cloud.db'));
  made.push({ dir, store });
  return store;
}
after(() => made.forEach(({ dir, store }) => {
  store.close();
  fs.rmSync(dir, { recursive: true, force: true });
}));

const quiet = async (fn) => {
  const log = console.log;
  console.log = () => {};
  try {
    return await fn();
  } finally {
    console.log = log;
  }
};

describe('production', () => {
  const urls = { PHOENIX_CLOUD_DATA: '.', PHOENIX_CLOUD_URL: 'https://cloud.example.com', PHOENIX_CLOUD_PLAY_URL: 'https://play.example.com' };

  it('refuses to serve without https addresses', () => {
    assert.throws(() => loadConfig({ PHOENIX_CLOUD_DATA: '.' }, { mode: 'production' }), /PHOENIX_CLOUD_URL/);
    assert.throws(() => loadConfig({ ...urls, PHOENIX_CLOUD_PLAY_URL: 'http://play.example.com' }, { mode: 'production' }), /PHOENIX_CLOUD_PLAY_URL/);
    assert.equal(loadConfig(urls, { mode: 'production' }).secureCookies, true);
    // Commands from a shell don't serve, so they need not know.
    assert.doesNotThrow(() => loadConfig({ PHOENIX_CLOUD_DATA: '.' }, { mode: 'production', commandLine: true }));
  });

  it('never makes the admin with the published password', async () => {
    const store = freshStore();
    await quiet(() => ensureDefaultAdmin(store, loadConfig(urls, { mode: 'production' })));
    const admin = findAccount(store, DEFAULT_ADMIN.name);
    assert.equal(admin.is_admin, 1);
    assert.equal(await verifyPassword(DEFAULT_ADMIN.password, admin.password_hash), false);
    assert.equal(admin.password_is_default, 1);
  });

  it('makes the admin with PHOENIX_CLOUD_ADMIN_PASSWORD when given', async () => {
    const store = freshStore();
    await quiet(() => ensureDefaultAdmin(store, loadConfig({ ...urls, PHOENIX_CLOUD_ADMIN_PASSWORD: 'a long chosen one' }, { mode: 'production' })));
    const admin = findAccount(store, DEFAULT_ADMIN.name);
    assert.equal(await verifyPassword('a long chosen one', admin.password_hash), true);
    assert.equal(admin.password_is_default, 0);
  });
});
