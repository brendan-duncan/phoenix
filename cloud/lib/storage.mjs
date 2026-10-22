// Document bytes: saving an uploaded .phx as a revision (its chunks stored
// once by hash, read from the upload's file a chunk at a time), rebuilding a
// revision's file as it is sent, quotas and garbage collection. Neither way
// holds a whole document in memory.

import { once } from 'node:events';
import fs from 'node:fs';
import { Readable } from 'node:stream';
import { HttpError } from './http.mjs';
import { sha256 } from './ids.mjs';
import {
  bodyOffset, BufferSource, checksumError, describe, FileSource, isThumbnail, layout, manifestEntry, manifestHashes, PhxError, readContainer,
  withThumbnailEntry,
} from './phx.mjs';

const MB = 1024 * 1024;

/** Bytes of distinct chunks in every revision of every document a space owns. */
export function usageBytes(store, ownerKind, ownerId) {
  return store.get(
    `SELECT COALESCE(SUM(b.size), 0) AS bytes FROM blobs b WHERE b.hash IN (
       SELECT rc.hash FROM revision_chunks rc
       JOIN revisions r ON r.id = rc.revision_id
       JOIN documents d ON d.id = r.document_id
       WHERE d.owner_kind = ? AND d.owner_id = ?)`, ownerKind, ownerId).bytes;
}

export function quotaBytes(store, ownerKind, ownerId) {
  const settings = store.settings;
  if (ownerKind === 'user') {
    const row = store.get('SELECT quota_mb FROM users WHERE id = ?', ownerId);
    return (row?.quota_mb ?? settings.defaultQuotaMB) * MB;
  }
  return settings.defaultQuotaMB * MB;
}

function ownsChunk(store, ownerKind, ownerId, hash) {
  return Boolean(store.get(
    `SELECT 1 FROM revision_chunks rc
     JOIN revisions r ON r.id = rc.revision_id
     JOIN documents d ON d.id = r.document_id
     WHERE rc.hash = ? AND d.owner_kind = ? AND d.owner_id = ? LIMIT 1`, hash, ownerKind, ownerId));
}

/** Refuses when adding chunks of `sizes` (hash → bytes) would pass the space's quota. */
function checkQuota(store, ownerKind, ownerId, sizes) {
  let added = 0;
  for (const [hash, size] of sizes) {
    if (!ownsChunk(store, ownerKind, ownerId, hash)) {
      added += size;
    }
  }
  if (added === 0) {
    return;
  }
  const quota = quotaBytes(store, ownerKind, ownerId);
  const used = usageBytes(store, ownerKind, ownerId);
  if (used + added > quota) {
    throw new HttpError(413, 'quota', `That needs ${Math.ceil((used + added - quota) / MB)} MB more than the ${Math.round(quota / MB)} MB of storage this space has.`);
  }
}

/** Refuses when `doc`'s stored chunks would pass the quota of the space it moves to. */
export function checkRoomForDocument(store, doc, ownerKind, ownerId) {
  const rows = store.all(
    `SELECT DISTINCT b.hash, b.size FROM revision_chunks rc
     JOIN revisions r ON r.id = rc.revision_id JOIN blobs b ON b.hash = rc.hash
     WHERE r.document_id = ?`, doc.id);
  checkQuota(store, ownerKind, ownerId, new Map(rows.map((r) => [r.hash, r.size])));
}

function insertRevision(store, doc, { manifest, size, application, label, userId, sizes }) {
  const current = store.get('SELECT revision FROM documents WHERE id = ?', doc.id).revision;
  const number = current + 1;
  const now = Date.now();
  for (const [hash, bytes] of sizes) {
    store.run('INSERT OR IGNORE INTO blobs (hash, size, created_at) VALUES (?, ?, ?)', hash, bytes, now);
  }
  const { lastInsertRowid } = store.run('INSERT INTO revisions (document_id, number, created_by, created_at, label, manifest, size, application) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
    doc.id, number, userId, now, String(label ?? '').slice(0, 120), JSON.stringify(manifest), size, application ?? '');
  for (const hash of manifestHashes(manifest)) {
    store.run('INSERT INTO revision_chunks (revision_id, hash) VALUES (?, ?)', lastInsertRowid, hash);
  }
  return { number, now };
}

/**
 * Reads the container in `source` a chunk at a time, staging each chunk's
 * stored bytes as a blob (checksums checked on the way); with `thumbnail`
 * (PNG bytes), that is its THMB. Nothing is kept until commitStaged().
 */
async function stageContainer(blobs, source, thumbnail) {
  const container = readContainer(source);
  const staged = new Map();
  let manifest = { v: 1, major: container.major, minor: container.minor, chunks: [] };
  if (container.flags) {
    manifest.flags = container.flags;
  }
  let thumb = null;
  const keep = async (stream) => {
    const blob = await blobs.stage(stream);
    if (staged.has(blob.hash)) {
      await blobs.discard(blob.temp);
    } else {
      staged.set(blob.hash, blob);
    }
    return blob;
  };
  try {
    for (const chunk of container.chunks) {
      const blob = await keep(source.stream(bodyOffset(chunk), chunk.storedLength));
      if (blob.crc !== chunk.crc || blob.size !== chunk.storedLength) {
        throw checksumError(chunk);
      }
      manifest.chunks.push(manifestEntry(chunk, blob.hash));
      if (isThumbnail(chunk)) {
        thumb = blob.hash;
      }
    }
    const metadata = describe(container, (chunk) => source.read(bodyOffset(chunk), chunk.storedLength));
    if (thumbnail) {
      const blob = await keep(Readable.from([thumbnail]));
      manifest = withThumbnailEntry(manifest, blob.hash, thumbnail);
      thumb = blob.hash;
    }
    return { manifest, staged, metadata, thumbnail: thumb, size: layout(manifest).size };
  } catch (err) {
    await Promise.all([...staged.values()].map((b) => blobs.discard(b.temp)));
    throw err;
  }
}

/**
 * Stores a .phx as the next revision of `doc`: `input` is { file } (a path,
 * read a piece at a time) or { buffer }, with an optional { thumbnail } PNG
 * to give it. With `base`, refuses (409) when someone saved since that
 * revision.
 */
export async function saveRevision(store, blobs, doc, input, { userId, base = null, label = '' }) {
  const source = input.file ? new FileSource(input.file) : new BufferSource(input.buffer);
  let split;
  try {
    split = await stageContainer(blobs, source, input.thumbnail ?? null);
  } catch (err) {
    if (err instanceof PhxError) {
      throw new HttpError(422, 'not_phx', `That file can't be stored: ${err.message}.`);
    }
    throw err;
  } finally {
    source.close();
  }
  const sizes = new Map([...split.staged.values()].map((b) => [b.hash, b.size]));
  try {
    checkQuota(store, doc.owner_kind, doc.owner_id, sizes);
  } catch (err) {
    await Promise.all([...split.staged.values()].map((b) => blobs.discard(b.temp)));
    throw err;
  }
  // Blob rows, then files, then the revision: if anything fails part way,
  // the blobs are only garbage that collectGarbage finds by their rows.
  // Touching created_at keeps a blob this upload reuses out of a collection
  // that runs before the revision is written.
  store.transaction(() => {
    const now = Date.now();
    for (const [hash, bytes] of sizes) {
      store.run('INSERT INTO blobs (hash, size, created_at) VALUES (?, ?, ?) ON CONFLICT (hash) DO UPDATE SET created_at = excluded.created_at', hash, bytes, now);
    }
  });
  for (const blob of split.staged.values()) {
    await blobs.commit(blob.hash, blob.temp);
  }
  return store.transaction(() => {
    const current = store.get('SELECT revision FROM documents WHERE id = ?', doc.id).revision;
    if (base !== null && base !== current) {
      throw new HttpError(409, 'conflict', `Someone saved this document since you opened it (revision ${current}, yours was ${base}).`);
    }
    const { number, now } = insertRevision(store, doc, {
      manifest: split.manifest, size: split.size, application: split.metadata.application, label, userId, sizes,
    });
    store.run('UPDATE documents SET revision = ?, size = ?, thumbnail = ?, info = ?, modified_at = ?, modified_by = ? WHERE id = ?',
      number, split.size, split.thumbnail, JSON.stringify(split.metadata), now, userId, doc.id);
    return number;
  });
}

/**
 * Keeps `file` (a .phx the editor sent) as `userId`'s draft of `doc`: their
 * unsaved work, one per person per document, replacing the last. Drafts
 * share stored chunks with the document's revisions.
 */
export async function saveDraft(store, blobs, doc, userId, file) {
  const source = new FileSource(file);
  let split;
  try {
    split = await stageContainer(blobs, source, null);
  } catch (err) {
    if (err instanceof PhxError) {
      throw new HttpError(422, 'not_phx', `That draft can't be kept: ${err.message}.`);
    }
    throw err;
  } finally {
    source.close();
  }
  const now = Date.now();
  store.transaction(() => {
    for (const blob of split.staged.values()) {
      store.run('INSERT INTO blobs (hash, size, created_at) VALUES (?, ?, ?) ON CONFLICT (hash) DO UPDATE SET created_at = excluded.created_at', blob.hash, blob.size, now);
    }
  });
  for (const blob of split.staged.values()) {
    await blobs.commit(blob.hash, blob.temp);
  }
  store.transaction(() => {
    store.run('DELETE FROM drafts WHERE document_id = ? AND user_id = ?', doc.id, userId);
    store.run('INSERT INTO drafts (document_id, user_id, manifest, size, base_revision, saved_at) VALUES (?, ?, ?, ?, ?, ?)',
      doc.id, userId, JSON.stringify(split.manifest), split.size, doc.revision, now);
    for (const hash of manifestHashes(split.manifest)) {
      store.run('INSERT INTO draft_chunks (document_id, user_id, hash) VALUES (?, ?, ?)', doc.id, userId, hash);
    }
  });
  return { savedAt: now, size: split.size };
}

/** A person's draft of a document: { savedAt, baseRevision, size, file (laid out) }, or null. */
export function draftOf(store, doc, userId) {
  const row = store.get('SELECT * FROM drafts WHERE document_id = ? AND user_id = ?', doc.id, userId);
  return row ? { savedAt: row.saved_at, baseRevision: row.base_revision, size: row.size, file: layout(JSON.parse(row.manifest)) } : null;
}

export function deleteDraft(store, documentId, userId) {
  store.run('DELETE FROM drafts WHERE document_id = ? AND user_id = ?', documentId, userId);
}

/** Writes a laid-out file (revisionFile) to disk at `file`, a blob at a time. */
export async function writeFile(blobs, laidOut, file) {
  const out = fs.createWriteStream(file);
  await sendFile(blobs, laidOut, out);
  await once(out, 'close');
}

/** Keeps `bytes` (a picture: a preview, a profile picture) as a blob; its hash. */
export async function keepPicture(store, blobs, bytes) {
  const hash = sha256(bytes);
  store.run('INSERT INTO blobs (hash, size, created_at) VALUES (?, ?, ?) ON CONFLICT (hash) DO UPDATE SET created_at = excluded.created_at', hash, bytes.length, Date.now());
  await blobs.put(hash, bytes);
  return hash;
}

/** Makes revision `number` of `source` the next revision of `target` (restore, duplicate). */
export function copyRevision(store, source, number, target, { userId, label = '' }) {
  return store.transaction(() => {
    const rev = store.get('SELECT * FROM revisions WHERE document_id = ? AND number = ?', source.id, number);
    if (!rev) {
      throw new HttpError(404, 'not_found', 'No such revision.');
    }
    const manifest = JSON.parse(rev.manifest);
    const sizes = new Map(store.all(`SELECT b.hash, b.size FROM blobs b JOIN revision_chunks rc ON rc.hash = b.hash WHERE rc.revision_id = ?`, rev.id)
      .map((b) => [b.hash, b.size]));
    checkQuota(store, target.owner_kind, target.owner_id, sizes);
    const { number: made, now } = insertRevision(store, target, { manifest, size: rev.size, application: rev.application, label, userId, sizes });
    const thumb = manifest.chunks.find((c) => c.t === 'THMB' && (c.f & 0xf) === 0)?.h ?? null;
    store.run('UPDATE documents SET revision = ?, size = ?, thumbnail = ?, info = ?, modified_at = ?, modified_by = ? WHERE id = ?',
      made, rev.size, thumb, source.info, now, userId, target.id);
    return made;
  });
}

/**
 * The file of revision `number`, laid out to send: { size, parts }, each
 * part a Buffer or { hash } of a blob to stream (phx.mjs layout()).
 */
export function revisionFile(store, doc, number) {
  const rev = store.get('SELECT manifest FROM revisions WHERE document_id = ? AND number = ?', doc.id, number);
  if (!rev) {
    throw new HttpError(404, 'not_found', 'No such revision.');
  }
  return layout(JSON.parse(rev.manifest));
}

/** Writes a laid-out file to `out` (a response), a blob at a time. */
export async function sendFile(blobs, file, out) {
  for await (const piece of fileBytes(blobs, file)) {
    if (!out.write(piece)) {
      await once(out, 'drain');
    }
  }
  out.end();
}

/** A revision file's bytes in order, a stored chunk at a time. */
export async function* fileBytes(blobs, file) {
  for (const part of file.parts) {
    if (Buffer.isBuffer(part)) {
      yield part;
    } else {
      yield* blobs.stream(part.hash);
    }
  }
}

/**
 * Deletes blobs nothing uses (no revision, draft, preview or profile
 * picture), older than `graceMs` (so an upload in flight keeps its chunks).
 */
export async function collectGarbage(store, blobs, graceMs = 24 * 60 * 60 * 1000) {
  const unused = store.all(
    `SELECT hash, size FROM blobs WHERE created_at < ?
       AND hash NOT IN (SELECT hash FROM revision_chunks)
       AND hash NOT IN (SELECT hash FROM draft_chunks)
       AND hash NOT IN (SELECT preview FROM documents WHERE preview IS NOT NULL)
       AND hash NOT IN (SELECT avatar_blob FROM users WHERE avatar_blob IS NOT NULL)`, Date.now() - graceMs);
  let bytes = 0;
  for (const blob of unused) {
    await blobs.remove(blob.hash);
    store.run('DELETE FROM blobs WHERE hash = ?', blob.hash);
    bytes += blob.size;
  }
  return { blobs: unused.length, bytes };
}
