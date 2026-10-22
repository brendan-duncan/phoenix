// Document bytes: saving an uploaded .phx as a revision (its chunks stored
// once by hash), rebuilding a revision's file, quotas and garbage collection.

import { HttpError } from './http.mjs';
import { buildContainer, manifestHashes, PhxError, splitContainer } from './phx.mjs';

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
 * Stores `bytes` (a whole .phx) as the next revision of `doc`. With `base`,
 * refuses (409) when someone saved since that revision.
 */
export async function saveRevision(store, blobs, doc, bytes, { userId, base = null, label = '' }) {
  let split;
  try {
    split = splitContainer(bytes);
  } catch (err) {
    if (err instanceof PhxError) {
      throw new HttpError(422, 'not_phx', `That file can't be stored: ${err.message}.`);
    }
    throw err;
  }
  const sizes = new Map(split.parts.map((p) => [p.hash, p.body.length]));
  checkQuota(store, doc.owner_kind, doc.owner_id, sizes);
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
  for (const part of split.parts) {
    if (!blobs.has(part.hash)) {
      await blobs.put(part.hash, part.body);
    }
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

/** The file of revision `number`, as pieces to write in order. */
export async function revisionFile(store, blobs, doc, number) {
  const rev = store.get('SELECT manifest, size FROM revisions WHERE document_id = ? AND number = ?', doc.id, number);
  if (!rev) {
    throw new HttpError(404, 'not_found', 'No such revision.');
  }
  return buildContainer(JSON.parse(rev.manifest), (hash) => blobs.get(hash));
}

/** Deletes blobs no revision uses, older than `graceMs` (so an upload in flight keeps its chunks). */
export async function collectGarbage(store, blobs, graceMs = 24 * 60 * 60 * 1000) {
  const unused = store.all('SELECT hash, size FROM blobs WHERE created_at < ? AND hash NOT IN (SELECT hash FROM revision_chunks)', Date.now() - graceMs);
  let bytes = 0;
  for (const blob of unused) {
    await blobs.remove(blob.hash);
    store.run('DELETE FROM blobs WHERE hash = ?', blob.hash);
    bytes += blob.size;
  }
  return { blobs: unused.length, bytes };
}
