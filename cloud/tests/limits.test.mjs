// What keeps uploads and conversions from taking the server's memory:
// uploads to disk with limits, checks before Phoenix opens a file, and each
// person's hourly budget of conversions.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { after, before, describe, it } from 'node:test';
import { Converter } from '../lib/convert.mjs';
import { loadConfig } from '../lib/config.mjs';
import { makePhx, startServer } from './helpers.mjs';

/** A ZIP whose one entry says it unpacks to `claimed` bytes (a "zip bomb" FLA). */
function zipClaiming(claimed) {
  const name = Buffer.from('DOMDocument.xml');
  const data = Buffer.from('x');
  const local = Buffer.alloc(30);
  local.writeUInt32LE(0x04034b50, 0);
  local.writeUInt32LE(zlib.crc32(data), 14);
  local.writeUInt32LE(data.length, 18);
  local.writeUInt32LE(Math.min(claimed, 0xfffffffe), 22);
  local.writeUInt16LE(name.length, 26);
  const central = Buffer.alloc(46);
  central.writeUInt32LE(0x02014b50, 0);
  central.writeUInt32LE(zlib.crc32(data), 16);
  central.writeUInt32LE(data.length, 20);
  central.writeUInt32LE(Math.min(claimed, 0xfffffffe), 24);
  central.writeUInt16LE(name.length, 28);
  const directoryOffset = local.length + name.length + data.length;
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(1, 8);
  end.writeUInt16LE(1, 10);
  end.writeUInt32LE(central.length + name.length, 12);
  end.writeUInt32LE(directoryOffset, 16);
  return Buffer.concat([local, name, data, central, name, end]);
}

let server;
let me;
let admin;

const upload = (client, name, body) => client.request('POST', `/api/documents/upload?name=${encodeURIComponent(name)}`, { body, headers: { 'Content-Type': 'application/octet-stream' } });

before(async () => {
  server = await startServer({
    env: {
      PHOENIX_CLOUD_MAX_UPLOAD_MB: '3',
      PHOENIX_CLOUD_CONVERT_MAX_UPLOAD_MB: '1',
      PHOENIX_CLOUD_CONVERT_MAX_EXPANDED_MB: '100',
      PHOENIX_CLOUD_CONVERSIONS_PER_HOUR: '3',
    },
  });
  me = server.client();
  await me.signIn('uma');
  admin = server.client();
  await admin.signInWith('phoenix', 'animate');
});

after(() => server.close());

describe('uploads', () => {
  it('refuse a body over the limit, and leave nothing behind', async () => {
    const res = await upload(me, 'big.phx', Buffer.alloc(4 * 1024 * 1024));
    assert.equal(res.status, 413);
    assert.equal((await res.json()).error, 'too_large');
  });

  it('store a document from its file and give the same bytes back', async () => {
    const bytes = await makePhx({ symbols: Array.from({ length: 40 }, (_, i) => `S${i}`) });
    const { document: doc } = await me.upload('many.phx', bytes);
    const res = await me.request('GET', `/api/documents/${doc.id}/file`);
    assert.ok(Buffer.from(await res.arrayBuffer()).equals(bytes));
    assert.equal(Number(res.headers.get('content-length')), bytes.length);
  });

  it('keep no temporary files once answered', () => {
    const dir = path.join(server.dir, 'uploads');
    assert.deepEqual(fs.existsSync(dir) ? fs.readdirSync(dir) : [], []);
    assert.deepEqual(fs.readdirSync(path.join(server.dir, 'blobs', 'tmp')), []);
  });
});

describe('before Phoenix opens a file', () => {
  it('refuses an FLA that unpacks to too much', async () => {
    const res = await upload(admin, 'bomb.fla', zipClaiming(3 * 1024 * 1024 * 1024));
    assert.equal(res.status, 413);
    assert.match((await res.json()).message, /unpacks to/);
  });

  it('refuses a SWF whose header says it is too large', async () => {
    const swf = Buffer.from('CWS\x0a\x00\x00\x00\xf0xxxxxxxx', 'latin1');
    const res = await upload(admin, 'huge.swf', swf);
    assert.equal(res.status, 413);
    assert.match((await res.json()).message, /unpacks to/);
  });

  it('takes files to convert only up to their own, smaller limit', async () => {
    const res = await upload(admin, 'large.svg', Buffer.alloc(2 * 1024 * 1024, 32));
    assert.equal(res.status, 413);
  });
});

describe('conversions per hour', () => {
  it('run out for a person, though not for admins', async () => {
    const svg = Buffer.from('<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"/>');
    for (let i = 0; i < 3; i++) {
      // No Phoenix here, so each is refused after it is counted.
      assert.notEqual((await upload(me, `a${i}.svg`, svg)).status, 429);
    }
    const res = await upload(me, 'a4.svg', svg);
    assert.equal(res.status, 429);
    assert.equal(await me.fails('POST', '/api/documents', 429, { json: { name: 'More' } }), 'slow_down');
    // .phx uploads need no conversion and are not counted.
    await me.upload('still.phx', await makePhx({ document: 'still' }));
    assert.notEqual((await upload(admin, 'admin.svg', svg)).status, 429);
  });
});

describe('the watchdog', () => {
  it('stops a job that uses more memory than one job may', async () => {
    const config = loadConfig({ PHOENIX_CLOUD_DATA: server.dir, PHOENIX_CLOUD_CONVERT_MAX_MEMORY_MB: '100' }, { mode: 'dev' });
    // Node in Phoenix's place, taking 10 MB more every 50 ms and holding on.
    const converter = new Converter({ ...config, phoenix: process.execPath });
    const dir = fs.mkdtempSync(path.join(server.dir, 'watch-'));
    fs.mkdirSync(path.join(dir, 'settings'));
    const greedy = 'const a = []; setInterval(() => a.push(Buffer.alloc(10 * 1024 * 1024, 1)), 50);';
    const started = Date.now();
    const { outOfMemory } = await converter.run(dir, ['-e', greedy]);
    assert.equal(outOfMemory, true);
    assert.ok(Date.now() - started < 20000);
  });
});
