// New documents and converted uploads, made by the Phoenix build in this
// repository. Skipped when there is none (PHOENIX_CLOUD_PHOENIX names another).

import assert from 'node:assert/strict';
import fs from 'node:fs';
import { after, before, describe, it } from 'node:test';
import { loadConfig } from '../lib/config.mjs';
import { svgSize } from '../lib/convert.mjs';
import { buildContainer, splitContainer } from '../lib/phx.mjs';
import { startServer } from './helpers.mjs';

const phoenix = loadConfig({ PHOENIX_CLOUD_DATA: '.' }, { mode: 'dev' }).phoenix;
const skip = !phoenix && 'no Phoenix build (set PHOENIX_CLOUD_PHOENIX)';
const SVG = '<svg xmlns="http://www.w3.org/2000/svg" width="400" height="300"><rect x="20" y="20" width="200" height="120" fill="#e8590c"/></svg>';
const SWF = process.env.PHOENIX_CLOUD_TEST_SWF ?? 'D:/fla/shape_tweens.swf';
const FLA = process.env.PHOENIX_CLOUD_TEST_FLA ?? 'D:/fla/classic_tween.fla';

let server;
let me;

describe('svgSize', () => {
  it('reads width and height, units and viewBox', () => {
    assert.deepEqual(svgSize(SVG), { width: 400, height: 300 });
    assert.deepEqual(svgSize('<svg viewBox="0 0 640 480">'), { width: 640, height: 480 });
    assert.deepEqual(svgSize('<svg width="1in" height="72pt">'), { width: 96, height: 96 });
    assert.equal(svgSize('<html>'), null);
  });
});

describe('Phoenix on the server', { skip }, () => {
  before(async () => {
    server = await startServer({ phoenix: true });
    me = server.client();
    await me.signIn('olga');
  });

  after(() => server?.close());

  /** The stored document's file, split. */
  async function stored(doc) {
    return splitContainer(await fileOf(doc));
  }

  async function fileOf(doc) {
    const res = await me.request('GET', `/api/documents/${doc.id}/file`);
    return Buffer.from(await res.arrayBuffer());
  }

  let fresh;

  it('makes a new document with the size, rate and a thumbnail', async () => {
    const { document: doc } = await me.post('/api/documents', { name: 'Fresh', width: 1280, height: 720, fps: 30, background: '#336699' });
    fresh = doc;
    assert.equal(doc.revision, 1);
    assert.equal(doc.info.width, 1280);
    assert.equal(doc.info.height, 720);
    assert.equal(doc.info.fps, 30);
    assert.ok(doc.thumbnailUrl);
    assert.ok((await stored(doc)).thumbnail);
    assert.equal(await me.fails('POST', '/api/documents', 400, { json: { width: 0 } }), 'bad_request');
  });

  it('turns an SVG into a document the size of its page', async () => {
    const { document: doc } = await me.upload('Logo.svg', Buffer.from(SVG));
    assert.equal(doc.name, 'Logo');
    assert.equal(doc.info.width, 400);
    assert.equal(doc.info.height, 300);
    assert.ok(doc.thumbnailUrl);
  });

  it('turns an SWF into a document', { skip: !fs.existsSync(SWF) && `no ${SWF}` }, async () => {
    const { document: doc } = await me.upload('movie.swf', fs.readFileSync(SWF));
    assert.equal(doc.revision, 1);
    assert.ok(doc.thumbnailUrl);
  });

  it('turns an FLA into a document', { skip: !fs.existsSync(FLA) && `no ${FLA}` }, async () => {
    const { document: doc } = await me.upload('scene.fla', fs.readFileSync(FLA));
    assert.equal(doc.name, 'scene');
    assert.ok(doc.thumbnailUrl);
  });

  it('gives a .phx without a thumbnail one', async () => {
    // The new document from above, as Phoenix wrote it, less its thumbnail.
    const { manifest, parts } = splitContainer(await fileOf(fresh));
    const bodies = new Map(parts.map((p) => [p.hash, p.body]));
    const bare = Buffer.concat(await buildContainer({ ...manifest, chunks: manifest.chunks.filter((c) => c.t !== 'THMB') }, (h) => bodies.get(h)));
    assert.equal(splitContainer(bare).thumbnail, null);
    const { document: doc } = await me.upload('bare.phx', bare);
    assert.ok(doc.thumbnailUrl);
  });

  it('says what went wrong with a file it cannot read, and keeps nothing', async () => {
    const before = server.store.get('SELECT COUNT(*) AS n FROM documents').n;
    const res = await me.request('POST', '/api/documents/upload?name=broken.svg', { body: Buffer.from('not an svg'), headers: { 'Content-Type': 'application/octet-stream' } });
    assert.equal(res.status, 422);
    assert.match((await res.json()).message, /not an SVG/);
    assert.equal(await me.fails('POST', '/api/documents/upload?name=notes.txt', 422, { body: Buffer.from('hi'), headers: { 'Content-Type': 'application/octet-stream' } }), 'convert');
    assert.equal(server.store.get('SELECT COUNT(*) AS n FROM documents').n, before);
  });
});
