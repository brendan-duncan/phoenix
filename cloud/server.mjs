#!/usr/bin/env node
// Phoenix Cloud: accounts, documents, sharing and groups for the web editor.
// See cloud/README.md.
//
//   node server.mjs dev                        run on this PC (dev login, cloud/dev-data)
//   node server.mjs serve                      run in production (settings from the environment)
//   node server.mjs make-admin --email E       make an invite that makes its claimant an admin
//   node server.mjs make-admin --handle H      make an existing user an admin
//   node server.mjs invite [--email E] [--note N] [--days D]
//   node server.mjs seed [file.phx ...]        dev: sample users, a group and documents
//   node server.mjs gc                         delete stored chunks no revision uses
//   node server.mjs status                     print totals
//   node server.mjs reset                      dev: delete all dev data
//
// Commands other than serve read cloud/dev.env and work on the dev data
// unless PHOENIX_CLOUD_MODE=production is set.

import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createInvite, createUser, findUser } from './lib/accounts.mjs';
import { createApp } from './lib/app.mjs';
import { BlobStore } from './lib/blobs.mjs';
import { CLOUD_DIR, editorAvailable, loadConfig } from './lib/config.mjs';
import { newId } from './lib/ids.mjs';
import { Store } from './lib/store.mjs';
import { collectGarbage, saveRevision } from './lib/storage.mjs';

const HOUR = 60 * 60 * 1000;

function loadEnvFile(mode) {
  const file = process.env.PHOENIX_CLOUD_ENV ?? (mode === 'dev' ? path.join(CLOUD_DIR, 'dev.env') : null);
  if (file && file !== 'none' && fs.existsSync(file)) {
    process.loadEnvFile(file);
  }
}

function option(args, name) {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
}

/** The mode for commands other than serve: dev unless PHOENIX_CLOUD_MODE says otherwise. */
function toolMode() {
  return process.env.PHOENIX_CLOUD_MODE ?? 'dev';
}

function open(mode = toolMode()) {
  loadEnvFile(mode);
  const config = loadConfig(process.env, { mode });
  const store = new Store(path.join(config.dataDir, 'cloud.db'));
  const blobs = new BlobStore(path.join(config.dataDir, 'blobs'));
  return { config, store, blobs };
}

function listen(server, port, host) {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, () => resolve(server.address().port));
  });
}

async function serve(mode) {
  const { config, store, blobs } = open(mode);
  const app = createApp({ config, store, blobs });
  const cloud = http.createServer(app.handle);
  const play = http.createServer(app.handlePlay);
  // Uploads may take a while; idle keep-alive sockets go quickly.
  cloud.requestTimeout = 15 * 60 * 1000;
  cloud.keepAliveTimeout = 5000;
  const port = await listen(cloud, config.port, config.bind);
  const playPort = await listen(play, config.playPort, config.bind);
  console.log(`[cloud] ${config.mode} listening on ${config.bind}:${port} (play ${playPort})`);
  console.log(`[cloud] ${config.publicUrl}  data ${config.dataDir}`);
  if (config.mode === 'dev') {
    const providers = Object.keys(config.providers);
    console.log(`[cloud] sign-in: dev login${providers.length ? `, ${providers.join(', ')}` : ' (no OAuth clients in cloud/dev.env)'}`);
    console.log(`[cloud] editor: ${editorAvailable(config) ? config.editorDir : 'not built (python tools/web/build_web.py)'}`);
  }
  const tidy = () => app.sessions.purgeExpired();
  tidy();
  setInterval(tidy, HOUR).unref();
  const stop = () => {
    cloud.close();
    play.close();
    store.close();
    process.exit(0);
  };
  process.on('SIGINT', stop);
  process.on('SIGTERM', stop);
}

function makeAdmin(args) {
  const { config, store } = open();
  const handle = option(args, '--handle');
  if (handle) {
    const user = findUser(store, handle);
    if (!user) {
      throw new Error(`no active user @${handle}`);
    }
    store.run('UPDATE users SET is_admin = 1 WHERE id = ?', user.id);
    store.audit(null, 'user.admin', 'user', user.id, { admin: true, by: 'cli' });
    console.log(`@${user.handle} is an admin.`);
    return;
  }
  const email = option(args, '--email');
  if (!email) {
    throw new Error('make-admin needs --email or --handle');
  }
  const { token } = createInvite(store, { email, note: 'admin (from the command line)', makeAdmin: true, days: 7 });
  console.log(`Sign in from this link, as ${email}, within 7 days:\n${config.publicUrl}/invite/${token}`);
}

function invite(args) {
  const { config, store } = open();
  const days = option(args, '--days');
  const { token } = createInvite(store, { email: option(args, '--email') ?? null, note: option(args, '--note') ?? '', days: days === undefined ? 14 : Number(days) });
  console.log(`${config.publicUrl}/invite/${token}`);
}

/** Dev sample data: alice (admin), bob, carol; a group; folders; shares; the given .phx files. */
async function seed(files) {
  const { config, store, blobs } = open('dev');
  const user = (handle, isAdmin = false) => findUser(store, handle) ??
    createUser(store, { name: handle.charAt(0).toUpperCase() + handle.slice(1), email: `${handle}@dev.local`, handle, isAdmin });
  const alice = user('alice', true);
  const bob = user('bob');
  const carol = user('carol');
  const now = Date.now();
  let group = store.get("SELECT * FROM groups WHERE handle = 'studio'");
  if (!group) {
    const id = newId();
    store.run("INSERT INTO groups (id, handle, name, description, created_by, created_at) VALUES (?, 'studio', 'Studio', 'A sample group', ?, ?)", id, alice.id, now);
    store.run("INSERT INTO group_members (group_id, user_id, role, added_by, added_at) VALUES (?, ?, 'admin', ?, ?)", id, alice.id, alice.id, now);
    store.run("INSERT INTO group_members (group_id, user_id, role, added_by, added_at) VALUES (?, ?, 'member', ?, ?)", id, bob.id, alice.id, now);
    group = store.get('SELECT * FROM groups WHERE id = ?', id);
  }
  const folder = (ownerKind, ownerId, name) => {
    const found = store.get('SELECT id FROM folders WHERE owner_kind = ? AND owner_id = ? AND parent_id IS NULL AND name = ?', ownerKind, ownerId, name);
    if (found) {
      return found.id;
    }
    const id = newId();
    store.run('INSERT INTO folders (id, owner_kind, owner_id, parent_id, name, created_by, created_at) VALUES (?, ?, ?, NULL, ?, ?, ?)', id, ownerKind, ownerId, name, alice.id, now);
    return id;
  };
  const spaces = [
    { kind: 'user', id: alice.id, folder: null },
    { kind: 'user', id: alice.id, folder: folder('user', alice.id, 'Scenes') },
    { kind: 'group', id: group.id, folder: null },
    { kind: 'user', id: bob.id, folder: null },
  ];
  let made = 0;
  for (const [i, file] of files.entries()) {
    const space = spaces[i % spaces.length];
    const docId = newId();
    const name = path.basename(file).replace(/\.phx$/i, '');
    const owner = space.kind === 'user' ? space.id : alice.id;
    store.run('INSERT INTO documents (id, owner_kind, owner_id, folder_id, name, created_by, created_at, modified_at, modified_by) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)',
      docId, space.kind, space.id, space.folder, name, owner, now, now, owner);
    try {
      await saveRevision(store, blobs, store.get('SELECT * FROM documents WHERE id = ?', docId), fs.readFileSync(file), { userId: owner });
    } catch (err) {
      store.run('DELETE FROM documents WHERE id = ?', docId);
      console.log(`skipped ${file}: ${err.message}`);
      continue;
    }
    // Bob's documents are shared with carol; alice's first one with bob.
    if (space.kind === 'user' && space.id === bob.id) {
      store.run("INSERT INTO shares (document_id, principal_kind, principal_id, role, granted_by, granted_at) VALUES (?, 'user', ?, 'viewer', ?, ?)", docId, carol.id, bob.id, now);
    } else if (i === 0) {
      store.run("INSERT INTO shares (document_id, principal_kind, principal_id, role, granted_by, granted_at) VALUES (?, 'user', ?, 'editor', ?, ?)", docId, bob.id, alice.id, now);
    }
    made++;
  }
  console.log(`Seeded @alice (admin), @bob, @carol, group @studio and ${made} document(s) in ${config.dataDir}.`);
  if (files.length === 0) {
    console.log('Pass .phx files to add documents: node server.mjs seed D:/fla/*.phx');
  }
}

async function gc() {
  const { store, blobs } = open();
  const { blobs: count, bytes } = await collectGarbage(store, blobs);
  console.log(`Deleted ${count} unused chunk(s), ${(bytes / 1048576).toFixed(1)} MB.`);
}

function status() {
  const { config, store } = open();
  const n = (sql) => store.get(sql).n;
  const blobs = store.get('SELECT COUNT(*) AS n, COALESCE(SUM(size), 0) AS bytes FROM blobs');
  console.log(`data      ${config.dataDir}`);
  console.log(`users     ${n('SELECT COUNT(*) AS n FROM users')}`);
  console.log(`groups    ${n('SELECT COUNT(*) AS n FROM groups')}`);
  console.log(`documents ${n('SELECT COUNT(*) AS n FROM documents')}`);
  console.log(`revisions ${n('SELECT COUNT(*) AS n FROM revisions')}`);
  console.log(`chunks    ${blobs.n} (${(blobs.bytes / 1048576).toFixed(1)} MB)`);
}

function reset() {
  loadEnvFile('dev');
  const config = loadConfig(process.env, { mode: 'dev' });
  if (config.mode !== 'dev' || !config.dataDir.startsWith(CLOUD_DIR)) {
    throw new Error(`reset only deletes dev data inside ${CLOUD_DIR}`);
  }
  fs.rmSync(config.dataDir, { recursive: true, force: true });
  console.log(`Deleted ${config.dataDir}.`);
}

const [command = 'serve', ...args] = process.argv.slice(2);
const commands = {
  dev: () => serve('dev'),
  serve: () => serve(process.env.PHOENIX_CLOUD_MODE ?? 'production'),
  'make-admin': () => makeAdmin(args),
  invite: () => invite(args),
  seed: () => seed(args),
  gc,
  status,
  reset,
};
if (!commands[command]) {
  console.error(`Unknown command '${command}'. Commands: ${Object.keys(commands).join(', ')}`);
  process.exit(2);
}
try {
  await commands[command]();
} catch (err) {
  console.error(`[cloud] ${err.message}`);
  process.exit(1);
}
