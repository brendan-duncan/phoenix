// What Phoenix renders from a saved document, in the background or on
// demand: the animated preview a card plays when the pointer is over it,
// and the HTML5 bundle the player shows to people who may not open the
// document itself. Bundles are served from the separate play origin, which
// has no cookies, so a bundle's address carries a signed, expiring pass.

import crypto from 'node:crypto';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { newId } from './ids.mjs';
import { keepPicture, revisionFile, writeFile } from './storage.mjs';

const PASS_HOURS = 6;

/** Writes a document's current revision to a temporary file for Phoenix; its path. */
async function revisionToDisk(app, doc) {
  const file = path.join(app.config.dataDir, 'work', `${newId()}.phx`);
  await fsp.mkdir(path.dirname(file), { recursive: true });
  await writeFile(app.blobs, revisionFile(app.store, doc, doc.revision), file);
  return file;
}

const previewing = new Set();

/**
 * Makes the animated preview of `documentId`'s current revision when it
 * has none, in the background (nobody waits for it). Never throws.
 */
export function refreshPreview(app, documentId) {
  if (!app.converter.available || previewing.has(documentId)) {
    return;
  }
  previewing.add(documentId);
  (async () => {
    const doc = app.store.get('SELECT * FROM documents WHERE id = ?', documentId);
    if (!doc || !doc.revision || doc.preview_revision === doc.revision) {
      return;
    }
    const file = await revisionToDisk(app, doc);
    try {
      const info = JSON.parse(doc.info);
      const webp = await app.converter.preview(file, { width: info.width ?? 1, height: info.height ?? 1 });
      if (webp) {
        const hash = await keepPicture(app.store, app.blobs, webp);
        app.store.run('UPDATE documents SET preview = ?, preview_revision = ? WHERE id = ? AND revision = ?', hash, doc.revision, doc.id, doc.revision);
      }
    } finally {
      await fsp.rm(file, { force: true });
    }
  })().catch((err) => console.error('[cloud] preview failed:', err.message)).finally(() => previewing.delete(documentId));
}

/** The folder holding the HTML5 bundle of a document's revision. */
function bundleDir(app, documentId, revision) {
  return path.join(app.config.dataDir, 'bundles', documentId, String(revision));
}

/**
 * Makes sure the current revision of `doc` is published as HTML5 (once a
 * revision; earlier revisions' bundles go). Waits for Phoenix if it has to.
 */
export async function ensureBundle(app, doc) {
  const dir = bundleDir(app, doc.id, doc.revision);
  if (fs.existsSync(path.join(dir, 'index.html'))) {
    return dir;
  }
  const file = await revisionToDisk(app, doc);
  try {
    await app.converter.publish(file, dir);
  } finally {
    await fsp.rm(file, { force: true });
  }
  for (const old of fs.readdirSync(path.dirname(dir))) {
    if (old !== String(doc.revision)) {
      await fsp.rm(path.join(path.dirname(dir), old), { recursive: true, force: true });
    }
  }
  return dir;
}

/** The server's secret for signing passes, made on first use. */
function secret(app) {
  if (!app.passSecret) {
    const file = path.join(app.config.dataDir, 'pass.key');
    if (!fs.existsSync(file)) {
      fs.mkdirSync(app.config.dataDir, { recursive: true });
      fs.writeFileSync(file, crypto.randomBytes(32).toString('hex'), { mode: 0o600 });
    }
    app.passSecret = fs.readFileSync(file, 'utf8').trim();
  }
  return app.passSecret;
}

function sign(app, body) {
  return crypto.createHmac('sha256', secret(app)).update(body).digest('base64url');
}

/** A signed address on the play origin for a revision's bundle, good for a few hours. */
export function bundleUrl(app, doc) {
  const body = Buffer.from(`${doc.id}.${doc.revision}.${Date.now() + PASS_HOURS * 3600 * 1000}`).toString('base64url');
  return `${app.config.playUrl}/b/${body}.${sign(app, body)}/index.html`;
}

/** The bundle folder a pass opens, or null when it is forged or has run out. */
export function openPass(app, pass) {
  const [body, signature] = String(pass ?? '').split('.');
  if (!body || !signature) {
    return null;
  }
  const expected = sign(app, body);
  if (signature.length !== expected.length || !crypto.timingSafeEqual(Buffer.from(signature), Buffer.from(expected))) {
    return null;
  }
  const [documentId, revision, expires] = Buffer.from(body, 'base64url').toString('utf8').split('.');
  if (!/^[A-Za-z0-9_-]{12}$/.test(documentId ?? '') || !/^\d+$/.test(revision ?? '') || !(Number(expires) > Date.now())) {
    return null;
  }
  return bundleDir(app, documentId, Number(revision));
}
