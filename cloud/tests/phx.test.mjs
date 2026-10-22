import assert from 'node:assert/strict';
import fs from 'node:fs';
import { describe, it } from 'node:test';
import { buildContainer, chunkContent, PhxError, readContainer, splitContainer, storedBytes } from '../lib/phx.mjs';
import { makePhx } from './helpers.mjs';

async function rebuild(buf) {
  const { manifest, parts } = splitContainer(buf);
  const bodies = new Map(parts.map((p) => [p.hash, p.body]));
  return Buffer.concat(await buildContainer(manifest, (h) => bodies.get(h)));
}

describe('phx containers', () => {
  it('splits and rebuilds byte for byte', async () => {
    const buf = await makePhx({ symbols: ['A', 'B', 'C'] });
    assert.ok((await rebuild(buf)).equals(buf));
  });

  it('reads chunks, metadata and the thumbnail', async () => {
    const buf = await makePhx({ symbols: ['Hero'] });
    const container = readContainer(buf);
    assert.deepEqual(container.chunks.map((c) => c.symbolName || ''), ['', 'Hero', '', '']);
    const docu = container.chunks[0];
    assert.equal(chunkContent(storedBytes(buf, docu), docu).toString().slice(0, 9), 'document ');
    const split = splitContainer(buf);
    assert.equal(split.metadata.application, 'Phoenix 0.22.0');
    assert.equal(split.metadata.author, 'Tester');
    assert.match(split.thumbnail, /^[0-9a-f]{64}$/);
  });

  it('shares chunks between documents that have them in common', async () => {
    const a = splitContainer(await makePhx({ symbols: ['Same', 'OnlyA'], document: 'one' }));
    const b = splitContainer(await makePhx({ symbols: ['Same', 'OnlyB'], document: 'two' }));
    const hashes = (s) => new Set(s.manifest.chunks.map((c) => c.h));
    const common = [...hashes(a)].filter((h) => hashes(b).has(h));
    // The 'Same' symbol, the thumbnail and the metadata.
    assert.equal(common.length, 3);
  });

  it('refuses what is not whole', async () => {
    const buf = await makePhx();
    assert.throws(() => readContainer(Buffer.from('hello world, not a phx file at all......')), PhxError);
    const cut = buf.subarray(0, buf.length - 10);
    assert.throws(() => readContainer(cut), PhxError);
    const flipped = Buffer.from(buf);
    flipped[100] ^= 0xff;
    assert.throws(() => readContainer(flipped), /checksum|damaged/);
    const newer = Buffer.from(buf);
    newer.writeUInt16LE(2, 8);
    assert.throws(() => readContainer(newer), /newer Phoenix/);
  });

  // A file Phoenix itself wrote, when there is one on this machine.
  const real = process.env.PHOENIX_CLOUD_TEST_PHX ?? 'D:/fla/tutorial1.phx';
  it('round-trips a file Phoenix wrote', { skip: !fs.existsSync(real) && `no ${real}` }, async () => {
    const buf = fs.readFileSync(real);
    assert.ok((await rebuild(buf)).equals(buf));
    assert.match(splitContainer(buf).metadata.application ?? '', /^Phoenix /);
  });
});
