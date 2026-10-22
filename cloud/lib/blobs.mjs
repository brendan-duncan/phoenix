// Content-addressed blob storage on disk: each blob is a file named by the
// SHA-256 of its bytes, in two levels of folders (ab/cd/abcd...). A blob is
// written once and never changed, so revisions and copies of a document share
// every chunk they have in common. Kept behind this small interface so it can
// move to object storage later.

import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
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

  /** Writes `bytes` under `hash` unless it is there already. */
  async put(hash, bytes) {
    const file = this.pathFor(hash);
    if (fs.existsSync(file)) {
      return false;
    }
    await fsp.mkdir(path.dirname(file), { recursive: true });
    // Written aside and renamed, so a crash never leaves half a blob.
    const temp = path.join(this.dir, 'tmp', `${hash}.${newId()}`);
    await fsp.writeFile(temp, bytes);
    await fsp.rename(temp, file);
    return true;
  }

  get(hash) {
    return fsp.readFile(this.pathFor(hash));
  }

  async remove(hash) {
    await fsp.rm(this.pathFor(hash), { force: true });
  }
}
