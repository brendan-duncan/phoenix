// Content-addressed blob storage on disk: each blob is a file named by the
// SHA-256 of its bytes, in two levels of folders (ab/cd/abcd...). A blob is
// written once and never changed, so revisions and copies of a document share
// every chunk they have in common. Blobs go in and out as streams, so no
// chunk is held in memory whole. Kept behind this small interface so it can
// move to object storage (another machine) later.

import crypto from 'node:crypto';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import zlib from 'node:zlib';
import { newId } from './ids.mjs';

const HASH_RE = /^[0-9a-f]{64}$/;

export class BlobStore {
  constructor(dir) {
    this.dir = dir;
    fs.mkdirSync(path.join(dir, 'tmp'), { recursive: true });
  }

  pathFor(hash) {
    if (!HASH_RE.test(hash)) {
      throw new Error(`not a blob hash: ${hash}`);
    }
    return path.join(this.dir, hash.slice(0, 2), hash.slice(2, 4), hash);
  }

  has(hash) {
    return fs.existsSync(this.pathFor(hash));
  }

  /**
   * Writes `stream` aside, working out its SHA-256 and CRC-32 on the way:
   * { hash, size, crc, temp }. commit() keeps it, discard() drops it.
   */
  async stage(stream) {
    const temp = path.join(this.dir, 'tmp', newId());
    const hash = crypto.createHash('sha256');
    let crc = 0;
    let size = 0;
    const out = await fsp.open(temp, 'w');
    try {
      for await (const piece of stream) {
        hash.update(piece);
        crc = zlib.crc32(piece, crc);
        size += piece.length;
        await out.write(piece);
      }
    } catch (err) {
      await out.close();
      await fsp.rm(temp, { force: true });
      throw err;
    }
    await out.close();
    return { hash: hash.digest('hex'), size, crc: crc >>> 0, temp };
  }

  /** Keeps a staged blob under its hash (or drops it when that blob is there already). */
  async commit(hash, temp) {
    const file = this.pathFor(hash);
    if (fs.existsSync(file)) {
      await fsp.rm(temp, { force: true });
      return false;
    }
    await fsp.mkdir(path.dirname(file), { recursive: true });
    await fsp.rename(temp, file);
    return true;
  }

  discard(temp) {
    return fsp.rm(temp, { force: true });
  }

  /** Writes `bytes` (small: a thumbnail) under `hash` unless it is there already. */
  async put(hash, bytes) {
    const file = this.pathFor(hash);
    if (fs.existsSync(file)) {
      return false;
    }
    const temp = path.join(this.dir, 'tmp', `${hash}.${newId()}`);
    await fsp.writeFile(temp, bytes);
    return this.commit(hash, temp);
  }

  get(hash) {
    return fsp.readFile(this.pathFor(hash));
  }

  stream(hash) {
    return fs.createReadStream(this.pathFor(hash));
  }

  async remove(hash) {
    await fsp.rm(this.pathFor(hash), { force: true });
  }
}
