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

  it('lists the starter templates and tutorials', async () => {
    const { templates, tutorials } = await me.get('/api/starters');
    assert.ok(templates.some((t) => t.id === 'bouncing-ball' && t.thumbnailUrl));
    assert.ok(tutorials.some((t) => t.id === '01-first-animation' && t.minutes > 0 && t.tags.length));
    const picture = await me.request('GET', templates[0].thumbnailUrl);
    assert.equal(picture.headers.get('content-type'), 'image/png');
    assert.equal(await me.fails('GET', '/api/starters/templates/..%2F..%2Fsecret/thumbnail', 404), 'not_found');
  });

  it('makes a copy of a starter template, with its picture', async () => {
    const { document: doc } = await me.post('/api/documents', { template: 'bouncing-ball' });
    assert.equal(doc.name, 'Bouncing Ball');
    assert.equal(doc.revision, 1);
    assert.ok((await stored(doc)).thumbnail);
    assert.equal(await me.fails('POST', '/api/documents', 404, { json: { template: 'nope' } }), 'not_found');
  });

  it("makes a tutorial's starting document, new or its own", async () => {
    const fresh = await me.post('/api/documents', { tutorial: '01-first-animation' });
    assert.equal(fresh.tutorial, '01-first-animation');
    assert.equal(fresh.document.info.width, 1280);
    const own = await me.post('/api/documents', { tutorial: '05-symbols-in-place' });
    assert.equal(own.tutorial, '05-symbols-in-place');
    assert.equal(own.document.revision, 1);
    assert.ok(own.document.thumbnailUrl);
  });

  it('makes an animated preview after a document is stored', async () => {
    const { document: doc } = await me.post('/api/documents', { template: 'bouncing-ball' });
    let previewUrl = null;
    for (let i = 0; i < 100 && !previewUrl; i++) {
      await new Promise((r) => setTimeout(r, 200));
      previewUrl = (await me.get(`/api/documents/${doc.id}`)).document.previewUrl;
    }
    assert.ok(previewUrl, 'no preview');
    assert.equal((await me.request('GET', previewUrl)).headers.get('content-type'), 'image/webp');
  });

  it('plays a document as HTML5 for viewers who may not open it', async () => {
    const { document: doc } = await me.post('/api/documents', { template: 'bouncing-ball' });
    await me.patch(`/api/documents/${doc.id}`, { allowDownload: false });
    const viewer = server.client();
    await viewer.signIn('wes');
    await me.post(`/api/documents/${doc.id}/shares`, { kind: 'user', who: 'wes', role: 'viewer' });
    const play = await viewer.get(`/api/documents/${doc.id}/play`);
    assert.equal(play.mode, 'bundle');
    assert.ok(play.url.startsWith(server.config.playUrl));
    const page = await fetch(play.url);
    assert.equal(page.status, 200);
    assert.match(await page.text(), /<script/);
    assert.equal(page.headers.get('set-cookie'), null);
    // A pass that was not signed here opens nothing.
    const forged = play.url.replace(/\/b\/[^/]+\./, '/b/AAAA.');
    assert.equal((await fetch(forged)).status, 404);
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
