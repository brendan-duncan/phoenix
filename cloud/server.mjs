#!/usr/bin/env node
// Phoenix Cloud: accounts, documents, sharing and groups, with the web
// editor. See cloud/README.md.
//
//   node server.mjs dev                               run on this PC (cloud/dev-data)
//   node server.mjs serve                             run in production (settings from the environment)
//   node server.mjs import [--to HANDLE] FILE ...     add .phx/.fla/.swf/.svg/.eps/.ai files to an account
//   node server.mjs set-password --handle H --password P
//   node server.mjs make-admin --handle H             make an account an admin
//   node server.mjs invite [--email E] [--note N] [--days D] [--admin] [--no-send]
//   node server.mjs fonts                             download the starter font library
//   node server.mjs gc                                delete stored chunks nothing uses
//   node server.mjs status                            print totals
//   node server.mjs check                             what is set up, and a test of Phoenix converting
//   node server.mjs reset                             dev: delete all dev data
//
// A server with no accounts makes the admin @phoenix: password "animate" in
// dev; in production PHOENIX_CLOUD_ADMIN_PASSWORD, or a random one it prints.
// Commands other than serve read cloud/dev.env and work on the dev data
// unless PHOENIX_CLOUD_MODE=production is set.

import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createInvite, ensureDefaultAdmin, findAccount, sendInvite } from './lib/accounts.mjs';
import { createApp } from './lib/app.mjs';
import { BlobStore } from './lib/blobs.mjs';
import { CLOUD_DIR, editorAvailable, loadConfig } from './lib/config.mjs';
import { Converter } from './lib/convert.mjs';
import { installStarterFonts, listFonts } from './lib/fonts.mjs';
import { newId } from './lib/ids.mjs';
import { Mailer } from './lib/mail.mjs';
import { hashPassword, passwordProblem } from './lib/passwords.mjs';
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

/** The arguments that are not options or their values. */
function plain(args, withValues) {
  const out = [];
  for (let i = 0; i < args.length; i++) {
    if (withValues.includes(args[i])) {
      i++;
    } else if (!args[i].startsWith('--')) {
      out.push(args[i]);
    }
  }
  return out;
}

/** The mode for commands other than serve: dev unless PHOENIX_CLOUD_MODE says otherwise. */
function toolMode() {
  return process.env.PHOENIX_CLOUD_MODE ?? 'dev';
}

function open(mode = toolMode(), serving = false) {
  loadEnvFile(mode);
  // Commands run from a shell need not know the service's public address.
  const config = loadConfig(process.env, { mode, commandLine: !serving });
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
  const { config, store, blobs } = open(mode, true);
  const app = createApp({ config, store, blobs });
  await app.ready;
  const cloud = http.createServer(app.handle);
  const play = http.createServer(app.handlePlay);
  // Uploads and conversions may take a while; idle keep-alive sockets go quickly.
  cloud.requestTimeout = 15 * 60 * 1000;
  cloud.keepAliveTimeout = 5000;
  const port = await listen(cloud, config.port, config.bind);
  const playPort = await listen(play, config.playPort, config.bind);
  console.log(`[cloud] ${config.mode} listening on ${config.bind}:${port} (play ${playPort})`);
  console.log(`[cloud] ${config.publicUrl}  data ${config.dataDir}`);
  const providers = Object.keys(config.providers);
  console.log(`[cloud] sign-in: passwords${providers.length ? `, ${providers.join(', ')}` : ''}`);
  console.log(`[cloud] editor: ${editorAvailable(config) ? config.editorDir : 'not built (python tools/web/build_web.py)'}`);
  console.log(`[cloud] new documents and conversion: ${config.phoenix ?? 'off (no Phoenix build; set PHOENIX_CLOUD_PHOENIX)'}`);
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

/** Files into an account's My Files, converted by Phoenix where they are not .phx. */
async function importFiles(args) {
  const { config, store, blobs } = open();
  await ensureDefaultAdmin(store, config);
  const handle = option(args, '--to') ?? 'phoenix';
  const user = findAccount(store, handle);
  if (!user) {
    throw new Error(`no account @${handle}`);
  }
  const converter = new Converter(config);
  const files = plain(args, ['--to']);
  let made = 0;
  for (const file of files) {
    const ext = path.extname(file).slice(1).toLowerCase();
    const name = path.basename(file).replace(/\.[^.]+$/, '');
    try {
      const converted = await converter.fromUpload(path.resolve(file), ext);
      const id = newId();
      const now = Date.now();
      store.run("INSERT INTO documents (id, owner_kind, owner_id, name, created_by, created_at, modified_at, modified_by) VALUES (?, 'user', ?, ?, ?, ?, ?, ?)",
        id, user.id, name, user.id, now, now, user.id);
      try {
        await saveRevision(store, blobs, store.get('SELECT * FROM documents WHERE id = ?', id), converted, { userId: user.id });
      } catch (err) {
        store.run('DELETE FROM documents WHERE id = ?', id);
        throw err;
      } finally {
        // What Phoenix made, never the file given.
        if (path.resolve(converted.file) !== path.resolve(file)) {
          fs.rmSync(converted.file, { force: true });
        }
      }
      made++;
      console.log(`added ${name}`);
    } catch (err) {
      console.log(`skipped ${file}: ${err.message}`);
    }
  }
  console.log(`Added ${made} of ${files.length} file(s) to @${user.handle}.`);
}

async function setPassword(args) {
  const { config, store } = open();
  await ensureDefaultAdmin(store, config);
  const user = findAccount(store, option(args, '--handle') ?? '');
  const password = option(args, '--password');
  if (!user) {
    throw new Error('set-password needs --handle of an existing account');
  }
  const problem = passwordProblem(password);
  if (problem) {
    throw new Error(problem);
  }
  store.run('UPDATE users SET password_hash = ?, password_is_default = 0 WHERE id = ?', await hashPassword(password), user.id);
  store.run('DELETE FROM sessions WHERE user_id = ?', user.id);
  store.audit(null, 'user.password_reset', 'user', user.id, { by: 'cli' });
  console.log(`Set the password of @${user.handle}.`);
}

async function makeAdmin(args) {
  const { config, store } = open();
  await ensureDefaultAdmin(store, config);
  const user = findAccount(store, option(args, '--handle') ?? '');
  if (!user) {
    throw new Error('make-admin needs --handle of an existing account');
  }
  store.run("UPDATE users SET is_admin = 1, status = 'active' WHERE id = ?", user.id);
  store.audit(null, 'user.admin', 'user', user.id, { admin: true, by: 'cli' });
  console.log(`@${user.handle} is an admin.`);
}

async function invite(args) {
  const { config, store } = open();
  const days = option(args, '--days') === undefined ? 14 : Number(option(args, '--days'));
  const email = option(args, '--email') ?? null;
  const note = option(args, '--note') ?? '';
  const { token } = createInvite(store, { email, note, makeAdmin: args.includes('--admin'), days });
  const url = `${config.publicUrl}/invite/${token}`;
  console.log(url);
  // An invite for an address is emailed there (as Admin > Invites does), unless --no-send.
  if (email && !args.includes('--no-send')) {
    const { status, detail } = await sendInvite(new Mailer(config), config, { email, url, note, days });
    console.log(`Email to ${email}: ${status}${status === 'sent' ? '' : ` (${detail})`}`);
  }
}

async function fonts() {
  const { config } = open();
  const written = await installStarterFonts(config.fontsDir);
  console.log(`Put ${written.length} fonts in ${config.fontsDir}.`);
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
  console.log(`accounts  ${n('SELECT COUNT(*) AS n FROM users')} (${n("SELECT COUNT(*) AS n FROM users WHERE status LIKE 'pending%'")} waiting)`);
  console.log(`groups    ${n('SELECT COUNT(*) AS n FROM groups')}`);
  console.log(`documents ${n('SELECT COUNT(*) AS n FROM documents')}`);
  console.log(`revisions ${n('SELECT COUNT(*) AS n FROM revisions')}`);
  console.log(`chunks    ${blobs.n} (${(blobs.bytes / 1048576).toFixed(1)} MB)`);
}

/**
 * What the service will use (addresses, data, editor, Phoenix, fonts), then
 * Phoenix made to do what the server asks of it: a new document, its
 * animated preview and its HTML5 version. Exits 1 when something is wrong.
 */
async function check() {
  const { config } = open();
  let ok = true;
  const say = (good, label, text) => {
    ok &&= good;
    console.log(`${good ? 'ok  ' : 'FAIL'} ${label.padEnd(10)} ${text}`);
  };
  say(true, 'address', `${config.publicUrl} (play ${config.playUrl})`);
  say(fs.existsSync(config.dataDir), 'data', config.dataDir);
  say(editorAvailable(config), 'editor', config.editorDir);
  say(Boolean(config.phoenix), 'phoenix', config.phoenix ?? 'not found (set PHOENIX_CLOUD_PHOENIX)');
  const fonts = fs.existsSync(config.fontsDir) ? listFonts(config.fontsDir) : [];
  say(true, 'fonts', `${fonts.length} in ${config.fontsDir}${fonts.length ? '' : ' (run: fonts)'}`);
  if (config.phoenix) {
    const converter = new Converter(config);
    say(converter.sandbox === 'bwrap' || process.platform !== 'linux', 'sandbox',
      converter.sandbox === 'bwrap' ? `bubblewrap, seeing ${converter.sandboxReadOnly.join(', ')}` : 'none (install bubblewrap: apt install bubblewrap)');
    const started = Date.now();
    try {
      const made = await converter.newDocument({ width: 320, height: 240, fps: 24, background: '#ffffff' });
      say(fs.existsSync(made.file) && made.thumbnail?.length > 0, 'convert', `new document in ${Date.now() - started} ms`);
      const preview = await converter.preview(made.file, { width: 320, height: 240 });
      say(Boolean(preview?.length), 'preview', preview ? `${preview.length} bytes` : 'none');
      const target = path.join(config.dataDir, 'work', `check-${newId()}`);
      await converter.publish(made.file, target);
      say(fs.existsSync(path.join(target, 'index.html')), 'html5', 'published');
      fs.rmSync(target, { recursive: true, force: true });
      fs.rmSync(made.file, { force: true });
    } catch (err) {
      say(false, 'convert', err.message);
    }
  }
  if (!ok) {
    process.exitCode = 1;
  }
}

function reset() {
  loadEnvFile('dev');
  const config = loadConfig(process.env, { mode: 'dev' });
  if (!config.dataDir.startsWith(CLOUD_DIR)) {
    throw new Error(`reset only deletes dev data inside ${CLOUD_DIR}`);
  }
  fs.rmSync(config.dataDir, { recursive: true, force: true });
  console.log(`Deleted ${config.dataDir}.`);
}

const [command = 'serve', ...args] = process.argv.slice(2);
const commands = {
  dev: () => serve('dev'),
  serve: () => serve(process.env.PHOENIX_CLOUD_MODE ?? 'production'),
  import: () => importFiles(args),
  'set-password': () => setPassword(args),
  'make-admin': () => makeAdmin(args),
  invite: () => invite(args),
  fonts,
  gc,
  status,
  check,
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
