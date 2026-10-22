// Reading and rebuilding .phx containers, without any of Phoenix's code. The
// server never needs a chunk's meaning: it checks a container is whole, keeps
// each chunk's stored bytes once by hash, and rebuilds the file from a
// manifest. Only DOCU (the stage), META (the application) and THMB (the PNG
// thumbnail) are looked inside. docs/phx-format.md describes the layout; rebuilding follows
// ContainerWriter (src/phx/container.cpp) so a file Phoenix wrote comes back
// byte for byte.

import fs from 'node:fs';
import { Readable } from 'node:stream';
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
// The most of an index, and of a chunk the server looks inside, read into memory.
const MAX_INDEX = 64 * 1024 * 1024;
const MAX_LOOK = 32 * 1024 * 1024;

export class PhxError extends Error {}

export function fourcc(name) {
  return Buffer.from(name, 'latin1').readUInt32LE(0);
}

export function typeName(type) {
  const b = Buffer.alloc(4);
  b.writeUInt32LE(type >>> 0);
  return [...b].map((c) => (c >= 32 && c < 127 ? String.fromCharCode(c) : '?')).join('');
}

const DOCU = fourcc('DOCU');
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

// ── Sources: where a container's bytes are read from ──────────────────────

/** A container in memory. */
export class BufferSource {
  constructor(buf) {
    this.buf = buf;
    this.size = buf.length;
  }

  read(offset, length) {
    return this.buf.subarray(offset, offset + length);
  }

  /** The bytes from `offset` as a stream. */
  stream(offset, length) {
    return Readable.from(length ? [this.buf.subarray(offset, offset + length)] : []);
  }

  close() {}
}

/**
 * A container in a file, read a piece at a time: an upload of any size is
 * never held in memory whole. The small reads (header, index, chunk
 * headers) are synchronous; chunk bodies come as streams.
 */
export class FileSource {
  constructor(file) {
    this.file = file;
    this.fd = fs.openSync(file, 'r');
    this.size = fs.fstatSync(this.fd).size;
  }

  read(offset, length) {
    const out = Buffer.alloc(length);
    let done = 0;
    while (done < length) {
      const n = fs.readSync(this.fd, out, done, length - done, offset + done);
      if (n === 0) {
        break;
      }
      done += n;
    }
    return out.subarray(0, done);
  }

  stream(offset, length) {
    return length ? fs.createReadStream(this.file, { start: offset, end: offset + length - 1 }) : Readable.from([]);
  }

  close() {
    fs.closeSync(this.fd);
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

function fits(size, offset, length) {
  return offset >= 0 && length >= 0 && offset <= size && length <= size - offset;
}

function readIndexAt(source, offset) {
  if (!fits(source.size, offset, CHUNK_HEADER_SIZE)) {
    return null;
  }
  const header = source.read(offset, CHUNK_HEADER_SIZE);
  if (header.readUInt32LE(0) !== INDX) {
    return null;
  }
  const stored = read64(header, 8);
  if (!fits(source.size, offset + CHUNK_HEADER_SIZE, stored) || stored > MAX_INDEX) {
    return null;
  }
  const body = source.read(offset + CHUNK_HEADER_SIZE, stored);
  if (crc32(body) !== header.readUInt32LE(24)) {
    return null;
  }
  return decodeIndex(body);
}

/**
 * Checks the structure of the container in `source` (a BufferSource or a
 * FileSource) and lists its chunks in file order. Each chunk's own checksum
 * is checked as its bytes are read (storage.mjs, splitContainer). Throws
 * PhxError for anything damaged: the server stores only whole files
 * (Phoenix's own reader can recover damaged ones; saving again there fixes
 * them).
 */
export function readContainer(source) {
  if (source.size < HEADER_SIZE + TRAILER_SIZE || !isPhx(source.read(0, MAGIC.length))) {
    throw new PhxError('not a Phoenix document');
  }
  const header = source.read(0, HEADER_SIZE);
  const major = header.readUInt16LE(8);
  const minor = header.readUInt16LE(10);
  if (major > MAX_MAJOR) {
    throw new PhxError(`made by a newer Phoenix (format ${major}.${minor})`);
  }
  let chunks = null;
  if (crc32(header.subarray(0, 24)) === header.readUInt32LE(24)) {
    chunks = readIndexAt(source, read64(header, 16));
  }
  if (!chunks) {
    throw new PhxError('damaged: its header or index does not check out; open it in Phoenix and save it again');
  }
  const trailer = source.read(source.size - TRAILER_SIZE, TRAILER_SIZE);
  if (!trailer.subarray(8, 12).equals(TRAILER_MARK) || crc32(trailer.subarray(0, 12)) !== trailer.readUInt32LE(12)) {
    throw new PhxError('damaged: its end is missing (was the upload cut short?)');
  }
  for (const chunk of chunks) {
    if (!fits(source.size, chunk.offset, CHUNK_HEADER_SIZE) || !fits(source.size, chunk.offset + CHUNK_HEADER_SIZE, chunk.storedLength)) {
      throw new PhxError(`damaged: chunk ${chunk.id} lies outside the file`);
    }
    const own = source.read(chunk.offset, CHUNK_HEADER_SIZE);
    if (own.readUInt32LE(0) !== chunk.type || read64(own, 8) !== chunk.storedLength || own.readUInt32LE(24) !== chunk.crc) {
      throw new PhxError(`damaged: chunk ${chunk.id} does not match the index`);
    }
    if ((chunk.flags & COMPRESSION_MASK) > DEFLATE) {
      throw new PhxError(`chunk ${chunk.id} uses a compression the server does not know`);
    }
  }
  chunks.sort((a, b) => a.offset - b.offset);
  return { major, minor, flags: header.readUInt32LE(12), chunks };
}

/** Where a chunk's stored bytes start in its file. */
export function bodyOffset(chunk) {
  return chunk.offset + CHUNK_HEADER_SIZE;
}

export function checksumError(chunk) {
  return new PhxError(`damaged: chunk ${chunk.id} (${typeName(chunk.type)}) fails its checksum`);
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

/** DOCU's stage size and frame rate (Document fields 1 to 3, with their defaults). */
export function decodeStage(content) {
  const out = { width: 550, height: 400, fps: 24 };
  const wire = new Wire(content);
  for (let f = wire.next(); f; f = wire.next()) {
    if (f.field === 1 && f.type === 0) {
      out.width = f.value;
    } else if (f.field === 2 && f.type === 0) {
      out.height = f.value;
    } else if (f.field === 3 && f.type === 1) {
      out.fps = Math.round(f.value.readDoubleLE(0) * 100) / 100;
    }
  }
  return out;
}

/** A chunk's manifest entry, its stored bytes being `hash`. */
export function manifestEntry(chunk, hash) {
  const entry = { t: typeName(chunk.type), f: chunk.flags, n: chunk.length, s: chunk.storedLength, c: chunk.crc, h: hash };
  if (chunk.mediaId) {
    entry.m = chunk.mediaId;
  }
  if (chunk.symbolName) {
    entry.y = chunk.symbolName;
  }
  return entry;
}

/**
 * What the server shows of a document: the application that wrote it and
 * its stage size and frame rate, from META and the first DOCU. `bodyOf`
 * gives a chunk's stored bytes. A DOCU too large to look into leaves the
 * stage unknown.
 */
export function describe(container, bodyOf) {
  let metadata = {};
  const stage = {};
  const docu = container.chunks.find((c) => c.type === DOCU);
  if (docu && docu.storedLength <= MAX_LOOK && docu.length <= MAX_LOOK) {
    try {
      Object.assign(stage, decodeStage(chunkContent(bodyOf(docu), docu)));
    } catch {
      // The size stays unknown; the document is still stored.
    }
  }
  const meta = container.chunks.find((c) => c.type === META);
  if (meta && meta.storedLength <= MAX_LOOK && meta.length <= MAX_LOOK) {
    try {
      metadata = decodeMetadata(chunkContent(bodyOf(meta), meta));
    } catch {
      metadata = {};
    }
  }
  return { ...metadata, ...stage };
}

/** Whether a chunk is a thumbnail the server can hand out as it is (an uncompressed THMB). */
export function isThumbnail(chunk) {
  return chunk.type === THMB && (chunk.flags & COMPRESSION_MASK) === 0;
}

/**
 * Splits a container held in memory: each chunk's stored bytes with their
 * hash (checksums checked), the manifest that rebuilds the file, and its
 * metadata and thumbnail hash. For small containers; uploads are stored a
 * chunk at a time from their files (storage.mjs).
 */
export function splitContainer(buf) {
  const source = new BufferSource(buf);
  const container = readContainer(source);
  const parts = [];
  const manifest = { v: 1, major: container.major, minor: container.minor, chunks: [] };
  if (container.flags) {
    manifest.flags = container.flags;
  }
  let thumbnail = null;
  for (const chunk of container.chunks) {
    const body = source.read(bodyOffset(chunk), chunk.storedLength);
    if (crc32(body) !== chunk.crc) {
      throw checksumError(chunk);
    }
    const hash = sha256(body);
    parts.push({ hash, body });
    manifest.chunks.push(manifestEntry(chunk, hash));
    if (isThumbnail(chunk)) {
      thumbnail = hash;
    }
  }
  const metadata = describe(container, (chunk) => source.read(bodyOffset(chunk), chunk.storedLength));
  return { manifest, parts, metadata, thumbnail, size: buf.length };
}

/** Every distinct chunk hash a manifest uses. */
export function manifestHashes(manifest) {
  return [...new Set(manifest.chunks.map((c) => c.h))];
}

/** `manifest` with `png` (stored under `hash`) as its thumbnail, placed before META as Phoenix writes it. */
export function withThumbnailEntry(manifest, hash, png) {
  const chunks = manifest.chunks.filter((c) => c.t !== 'THMB');
  const thumb = { t: 'THMB', f: 0, n: png.length, s: png.length, c: crc32(png), h: hash };
  const meta = chunks.findIndex((c) => c.t === 'META');
  chunks.splice(meta < 0 ? chunks.length : meta, 0, thumb);
  return { ...manifest, chunks };
}

// ── Rebuilding ────────────────────────────────────────────────────────────

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
 * How the file a manifest rebuilds to is laid out, as ContainerWriter
 * writes it: `parts` in order, each a Buffer or { hash, size } for a
 * chunk's stored bytes, and the file's `size`. No chunk is read.
 */
export function layout(manifest) {
  const parts = [null];
  let offset = HEADER_SIZE;
  const offsets = [];
  const pad = () => {
    if (offset % 8) {
      const n = 8 - (offset % 8);
      parts.push(Buffer.alloc(n));
      offset += n;
    }
  };
  manifest.chunks.forEach((c, i) => {
    pad();
    offsets.push(offset);
    parts.push(chunkHeader(fourcc(c.t), c.f, c.s, c.n, c.c, i + 1), { hash: c.h, size: c.s });
    offset += CHUNK_HEADER_SIZE + c.s;
  });
  const index = encodeIndex(manifest.chunks, (i) => offsets[i]);
  pad();
  const indexOffset = offset;
  parts.push(chunkHeader(INDX, CRITICAL, index.length, index.length, crc32(index), manifest.chunks.length + 1), index);
  offset += CHUNK_HEADER_SIZE + index.length;

  const trailer = Buffer.alloc(TRAILER_SIZE);
  trailer.writeBigUInt64LE(BigInt(indexOffset), 0);
  TRAILER_MARK.copy(trailer, 8);
  trailer.writeUInt32LE(crc32(trailer.subarray(0, 12)), 12);
  parts.push(trailer);
  offset += TRAILER_SIZE;

  const header = Buffer.alloc(HEADER_SIZE);
  MAGIC.copy(header, 0);
  header.writeUInt16LE(manifest.major, 8);
  header.writeUInt16LE(manifest.minor, 10);
  header.writeUInt32LE(manifest.flags ?? 0, 12);
  header.writeBigUInt64LE(BigInt(indexOffset), 16);
  header.writeUInt32LE(crc32(header.subarray(0, 24)), 24);
  parts[0] = header;
  return { parts, size: offset };
}

/** Rebuilds a container in memory; `bodyOf(hash)` gives each chunk's stored bytes. */
export async function buildContainer(manifest, bodyOf) {
  const pieces = [];
  for (const part of layout(manifest).parts) {
    if (Buffer.isBuffer(part)) {
      pieces.push(part);
    } else {
      const body = await bodyOf(part.hash);
      if (body.length !== part.size) {
        throw new PhxError(`stored chunk ${part.hash} has the wrong size`);
      }
      pieces.push(body);
    }
  }
  return pieces;
}
