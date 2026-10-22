// Reading and rebuilding .phx containers, without any of Phoenix's code. The
// server never needs a chunk's meaning: it checks a container is whole, keeps
// each chunk's stored bytes once by hash, and rebuilds the file from a
// manifest. Only META (title, application) and THMB (the PNG thumbnail) are
// looked inside. docs/phx-format.md describes the layout; rebuilding follows
// ContainerWriter (src/phx/container.cpp) so a file Phoenix wrote comes back
// byte for byte.

import zlib from 'node:zlib';
import { sha256 } from './ids.mjs';

export const MAGIC = Buffer.from([0x89, 0x50, 0x48, 0x58, 0x0d, 0x0a, 0x1a, 0x0a]);
export const MAX_MAJOR = 1;
const HEADER_SIZE = 32;
const CHUNK_HEADER_SIZE = 32;
const TRAILER_SIZE = 16;
const TRAILER_MARK = Buffer.from('PHX!', 'latin1');
const DEFLATE = 1;
const COMPRESSION_MASK = 0xf;
const CRITICAL = 0x10;

export class PhxError extends Error {}

export function fourcc(name) {
  return Buffer.from(name, 'latin1').readUInt32LE(0);
}

export function typeName(type) {
  const b = Buffer.alloc(4);
  b.writeUInt32LE(type >>> 0);
  return [...b].map((c) => (c >= 32 && c < 127 ? String.fromCharCode(c) : '?')).join('');
}

const INDX = fourcc('INDX');
const META = fourcc('META');
const THMB = fourcc('THMB');

export function isPhx(buf) {
  return buf.length >= MAGIC.length && buf.subarray(0, MAGIC.length).equals(MAGIC);
}

function crc32(bytes) {
  return zlib.crc32(bytes) >>> 0;
}

function read64(buf, at) {
  const v = buf.readBigUInt64LE(at);
  if (v > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new PhxError('a length or offset is too large');
  }
  return Number(v);
}

function fits(buf, offset, length) {
  return offset >= 0 && length >= 0 && offset <= buf.length && length <= buf.length - offset;
}

// ── Protobuf wire format, the little this needs ───────────────────────────

class Wire {
  constructor(bytes) {
    this.bytes = bytes;
    this.at = 0;
  }

  varint() {
    let value = 0;
    let scale = 1;
    for (let i = 0; i < 10; i++) {
      if (this.at >= this.bytes.length) {
        throw new PhxError('a message ends inside a number');
      }
      const b = this.bytes[this.at++];
      value += (b & 0x7f) * scale;
      if (!(b & 0x80)) {
        if (!Number.isSafeInteger(value)) {
          throw new PhxError('a number is too large');
        }
        return value;
      }
      scale *= 128;
    }
    throw new PhxError('a number is too long');
  }

  /** The next field as { field, type, value }, or null at the end. */
  next() {
    if (this.at >= this.bytes.length) {
      return null;
    }
    const key = this.varint();
    const field = Math.floor(key / 8);
    const type = key % 8;
    let value;
    if (type === 0) {
      value = this.varint();
    } else if (type === 1) {
      this.need(8);
      value = this.bytes.subarray(this.at, this.at + 8);
      this.at += 8;
    } else if (type === 2) {
      const length = this.varint();
      this.need(length);
      value = this.bytes.subarray(this.at, this.at + length);
      this.at += length;
    } else if (type === 5) {
      this.need(4);
      value = this.bytes.readUInt32LE(this.at);
      this.at += 4;
    } else {
      throw new PhxError(`a message uses wire type ${type}`);
    }
    return { field, type, value };
  }

  need(n) {
    if (n > this.bytes.length - this.at) {
      throw new PhxError('a message ends early');
    }
  }
}

class WireWriter {
  constructor() {
    this.parts = [];
  }

  varint(value) {
    const out = [];
    let v = value;
    while (v >= 0x80) {
      out.push((v % 128) | 0x80);
      v = Math.floor(v / 128);
    }
    out.push(v);
    this.parts.push(Buffer.from(out));
  }

  key(field, type) {
    this.varint(field * 8 + type);
  }

  uint(field, value) {
    this.key(field, 0);
    this.varint(value);
  }

  fixed32(field, value) {
    this.key(field, 5);
    const b = Buffer.alloc(4);
    b.writeUInt32LE(value >>> 0);
    this.parts.push(b);
  }

  bytes(field, value) {
    this.key(field, 2);
    this.varint(value.length);
    this.parts.push(value);
  }

  buffer() {
    return Buffer.concat(this.parts);
  }
}

// ── Reading ───────────────────────────────────────────────────────────────

function decodeIndex(bytes) {
  const chunks = [];
  const index = new Wire(bytes);
  for (let f = index.next(); f; f = index.next()) {
    if (f.field !== 1 || f.type !== 2) {
      continue;
    }
    const chunk = { id: 0, type: 0, offset: 0, storedLength: 0, length: 0, flags: 0, crc: 0, mediaId: 0, symbolName: '' };
    const entry = new Wire(f.value);
    for (let e = entry.next(); e; e = entry.next()) {
      switch (e.field) {
        case 1: chunk.id = e.value; break;
        case 2: chunk.type = e.value; break;
        case 3: chunk.offset = e.value; break;
        case 4: chunk.storedLength = e.value; break;
        case 5: chunk.length = e.value; break;
        case 6: chunk.flags = e.value; break;
        case 7: chunk.crc = e.value; break;
        case 8: chunk.mediaId = e.value; break;
        case 9: chunk.symbolName = e.value.toString('utf8'); break;
        default: break;
      }
    }
    chunks.push(chunk);
  }
  return chunks;
}

function readIndexAt(buf, offset) {
  if (!fits(buf, offset, CHUNK_HEADER_SIZE) || buf.readUInt32LE(offset) !== INDX) {
    return null;
  }
  const stored = read64(buf, offset + 8);
  if (!fits(buf, offset + CHUNK_HEADER_SIZE, stored)) {
    return null;
  }
  const body = buf.subarray(offset + CHUNK_HEADER_SIZE, offset + CHUNK_HEADER_SIZE + stored);
  if (crc32(body) !== buf.readUInt32LE(offset + 24)) {
    return null;
  }
  return decodeIndex(body);
}

/**
 * Checks `buf` is a whole .phx container and lists its chunks in file order.
 * Throws PhxError for anything damaged: the server stores only whole files
 * (Phoenix's own reader can recover damaged ones; re-saving there fixes them).
 */
export function readContainer(buf) {
  if (buf.length < HEADER_SIZE + TRAILER_SIZE || !isPhx(buf)) {
    throw new PhxError('not a Phoenix document');
  }
  const major = buf.readUInt16LE(8);
  const minor = buf.readUInt16LE(10);
  if (major > MAX_MAJOR) {
    throw new PhxError(`made by a newer Phoenix (format ${major}.${minor})`);
  }
  let chunks = null;
  if (crc32(buf.subarray(0, 24)) === buf.readUInt32LE(24)) {
    chunks = readIndexAt(buf, read64(buf, 16));
  }
  if (!chunks) {
    throw new PhxError('damaged: its header or index does not check out; open it in Phoenix and save it again');
  }
  const trailer = buf.subarray(buf.length - TRAILER_SIZE);
  if (!trailer.subarray(8, 12).equals(TRAILER_MARK) || crc32(trailer.subarray(0, 12)) !== trailer.readUInt32LE(12)) {
    throw new PhxError('damaged: its end is missing (was the upload cut short?)');
  }
  for (const chunk of chunks) {
    const start = chunk.offset + CHUNK_HEADER_SIZE;
    if (!fits(buf, chunk.offset, CHUNK_HEADER_SIZE) || !fits(buf, start, chunk.storedLength)) {
      throw new PhxError(`damaged: chunk ${chunk.id} lies outside the file`);
    }
    if (buf.readUInt32LE(chunk.offset) !== chunk.type || read64(buf, chunk.offset + 8) !== chunk.storedLength || buf.readUInt32LE(chunk.offset + 24) !== chunk.crc) {
      throw new PhxError(`damaged: chunk ${chunk.id} does not match the index`);
    }
    if ((chunk.flags & COMPRESSION_MASK) > DEFLATE) {
      throw new PhxError(`chunk ${chunk.id} uses a compression the server does not know`);
    }
    if (crc32(buf.subarray(start, start + chunk.storedLength)) !== chunk.crc) {
      throw new PhxError(`damaged: chunk ${chunk.id} (${typeName(chunk.type)}) fails its checksum`);
    }
  }
  chunks.sort((a, b) => a.offset - b.offset);
  return { major, minor, flags: buf.readUInt32LE(12), chunks };
}

export function storedBytes(buf, chunk) {
  const start = chunk.offset + CHUNK_HEADER_SIZE;
  return buf.subarray(start, start + chunk.storedLength);
}

/** A chunk's content, inflated if it was stored deflated. */
export function chunkContent(stored, chunk) {
  if ((chunk.flags & COMPRESSION_MASK) !== DEFLATE) {
    return stored;
  }
  // As Phoenix's plausibleLength: deflate shrinks by about 1032:1 at most.
  if (chunk.length > stored.length * 1032 + 1024) {
    throw new PhxError('a chunk claims an impossible size');
  }
  const out = zlib.inflateSync(stored, { maxOutputLength: Math.max(chunk.length, 1) });
  if (out.length !== chunk.length) {
    throw new PhxError('a chunk inflates to the wrong size');
  }
  return out;
}

/** META's application, created, modified and author, when present. */
export function decodeMetadata(content) {
  const out = {};
  const names = { 1: 'application', 2: 'created', 3: 'modified', 4: 'author' };
  const wire = new Wire(content);
  for (let f = wire.next(); f; f = wire.next()) {
    if (names[f.field] && f.type === 2) {
      out[names[f.field]] = f.value.toString('utf8').slice(0, 200);
    }
  }
  return out;
}

/**
 * Splits a container for storage: each chunk's stored bytes with their hash,
 * and the manifest that rebuilds the file from them. Also pulls out the
 * metadata and thumbnail hash.
 */
export function splitContainer(buf) {
  const container = readContainer(buf);
  const parts = [];
  const manifest = { v: 1, major: container.major, minor: container.minor, chunks: [] };
  if (container.flags) {
    manifest.flags = container.flags;
  }
  let metadata = {};
  let thumbnail = null;
  for (const chunk of container.chunks) {
    const body = storedBytes(buf, chunk);
    const hash = sha256(body);
    parts.push({ hash, body });
    const entry = { t: typeName(chunk.type), f: chunk.flags, n: chunk.length, s: chunk.storedLength, c: chunk.crc, h: hash };
    if (chunk.mediaId) {
      entry.m = chunk.mediaId;
    }
    if (chunk.symbolName) {
      entry.y = chunk.symbolName;
    }
    manifest.chunks.push(entry);
    if (chunk.type === META) {
      try {
        metadata = decodeMetadata(chunkContent(body, chunk));
      } catch {
        metadata = {};
      }
    } else if (chunk.type === THMB && (chunk.flags & COMPRESSION_MASK) === 0) {
      thumbnail = hash;
    }
  }
  return { manifest, parts, metadata, thumbnail, size: buf.length };
}

/** Every distinct chunk hash a manifest uses. */
export function manifestHashes(manifest) {
  return [...new Set(manifest.chunks.map((c) => c.h))];
}

function encodeIndex(entries, offsetOf) {
  const index = new WireWriter();
  entries.forEach((c, i) => {
    const entry = new WireWriter();
    entry.uint(1, i + 1);
    entry.fixed32(2, fourcc(c.t));
    entry.uint(3, offsetOf(i));
    entry.uint(4, c.s);
    entry.uint(5, c.n);
    if (c.f) {
      entry.uint(6, c.f);
    }
    entry.fixed32(7, c.c);
    if (c.m) {
      entry.uint(8, c.m);
    }
    if (c.y) {
      entry.bytes(9, Buffer.from(c.y, 'utf8'));
    }
    index.bytes(1, entry.buffer());
  });
  return index.buffer();
}

function chunkHeader(type, flags, storedLength, length, crc, id) {
  const h = Buffer.alloc(CHUNK_HEADER_SIZE);
  h.writeUInt32LE(type >>> 0, 0);
  h.writeUInt32LE(flags >>> 0, 4);
  h.writeBigUInt64LE(BigInt(storedLength), 8);
  h.writeBigUInt64LE(BigInt(length), 16);
  h.writeUInt32LE(crc >>> 0, 24);
  h.writeUInt32LE(id >>> 0, 28);
  return h;
}

/**
 * Rebuilds a container from a manifest. `bodyOf(hash)` gives each chunk's
 * stored bytes. Returns the pieces in order, so a caller can stream them.
 */
export async function buildContainer(manifest, bodyOf) {
  const pieces = [Buffer.alloc(HEADER_SIZE)];
  let offset = HEADER_SIZE;
  const offsets = [];
  const pad = () => {
    if (offset % 8) {
      const n = 8 - (offset % 8);
      pieces.push(Buffer.alloc(n));
      offset += n;
    }
  };
  for (let i = 0; i < manifest.chunks.length; i++) {
    const c = manifest.chunks[i];
    const body = await bodyOf(c.h);
    if (body.length !== c.s) {
      throw new PhxError(`stored chunk ${c.h} has the wrong size`);
    }
    pad();
    offsets.push(offset);
    pieces.push(chunkHeader(fourcc(c.t), c.f, c.s, c.n, c.c, i + 1), body);
    offset += CHUNK_HEADER_SIZE + body.length;
  }
  const index = encodeIndex(manifest.chunks, (i) => offsets[i]);
  pad();
  const indexOffset = offset;
  pieces.push(chunkHeader(INDX, CRITICAL, index.length, index.length, crc32(index), manifest.chunks.length + 1), index);

  const trailer = Buffer.alloc(TRAILER_SIZE);
  trailer.writeBigUInt64LE(BigInt(indexOffset), 0);
  TRAILER_MARK.copy(trailer, 8);
  trailer.writeUInt32LE(crc32(trailer.subarray(0, 12)), 12);
  pieces.push(trailer);

  const header = pieces[0];
  MAGIC.copy(header, 0);
  header.writeUInt16LE(manifest.major, 8);
  header.writeUInt16LE(manifest.minor, 10);
  header.writeUInt32LE(manifest.flags ?? 0, 12);
  header.writeBigUInt64LE(BigInt(indexOffset), 16);
  header.writeUInt32LE(crc32(header.subarray(0, 24)), 24);
  return pieces;
}
