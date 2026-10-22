// Documents, their revisions and files, folders, sharing, favorites and recents.

import fsp from 'node:fs/promises';
import { atLeast, documentRole, groupRole, mayDownload, mayOpen, SHARE_ROLES, spaceRole, VISIBILITIES } from '../access.mjs';
import { findUser, publicUser } from '../accounts.mjs';
import { editorAvailable } from '../config.mjs';
import { UPLOAD_KINDS } from '../convert.mjs';
import { badRequest, forbidden, HttpError, notFound } from '../http.mjs';
import { hashToken, newId, newToken } from '../ids.mjs';
import { adminIds, notify } from '../notify.mjs';
import { transferDocument } from '../ownership.mjs';
import { bundleUrl, ensureBundle, refreshPreview } from '../renders.mjs';
import { copyRevision, deleteDraft, draftOf, fileBytes, quotaBytes, revisionFile, saveDraft, saveRevision, sendFile, usageBytes } from '../storage.mjs';
import { writeZip, ZIP_LIMIT, zipSize } from '../zip.mjs';

const LIST_LIMIT = 500;
// Documents in one ZIP download.
const MAX_DOWNLOAD = 200;
// An edit lock lasts this long unless the editor keeps it.
const LOCK_MS = 90 * 1000;

function cleanName(value, fallback = 'Untitled') {
  const name = String(value ?? '').replace(/[\u0000-\u001f]/g, '').trim().slice(0, 120);
  return name || fallback;
}

/** "scene.phx" → "scene" */
function nameFromFile(file) {
  return cleanName(String(file ?? '').replace(/^.*[\\/]/, '').replace(/\.(phx|fla|swf|svg|eps|ai)$/i, ''));
}

function extensionOf(file) {
  return /\.([a-z0-9]+)$/i.exec(String(file ?? ''))?.[1]?.toLowerCase() ?? '';
}

function wholeNumber(value, low, high, what) {
  const n = Number(value);
  if (!Number.isFinite(n) || n < low || n > high) {
    throw badRequest(`${what} is from ${low} to ${high}.`);
  }
  return n;
}

function getDocument(store, id) {
  return store.get('SELECT * FROM documents WHERE id = ?', id) ?? null;
}

/**
 * The document and the caller's role on it; 404 when they have none. An
 * admin with no role of their own views it passively (ctx.adminView): as a
 * viewer, with nothing recorded as theirs (no recents, drafts or locks) and
 * nothing counted as a view. A removed document's owner reaches only what
 * `removedOk` routes allow (seeing it, and deleting it).
 */
function access(ctx, id, needed = 'viewer', { removedOk = false } = {}) {
  const doc = getDocument(ctx.store, id);
  let role = documentRole(ctx.store, ctx.user, doc, { linkToken: ctx.linkToken, beta: ctx.settings.beta });
  ctx.adminView = false;
  if (role === 'none' && doc && ctx.user?.is_admin) {
    role = 'viewer';
    ctx.adminView = true;
  }
  if (role === 'none') {
    throw ctx.user ? notFound('No such document, or it is not shared with you.') : new HttpError(401, 'signed_out', 'Sign in to see this document.');
  }
  if (doc.moderation === 'removed' && !ctx.user?.is_admin && !removedOk) {
    throw new HttpError(403, 'removed', `The moderators removed this document${doc.moderation_note ? `: ${doc.moderation_note}` : '.'}`);
  }
  if (!atLeast(role, needed)) {
    throw forbidden(needed === 'owner' ? 'Only its owner can do that.' : `That needs ${needed} access.`);
  }
  return { doc, role, adminView: ctx.adminView };
}

/** Whether the caller may have the file in the web editor (an admin's passive view may). */
function canOpen(ctx, doc, role) {
  return ctx.adminView || mayOpen(ctx.store, ctx.user, doc, role, ctx.linkToken);
}

// An admin's look at a document is written to the audit log once an hour.
const adminLooks = new Map();
function noteAdminView(ctx, doc) {
  const key = `${ctx.user.id}:${doc.id}`;
  const now = Date.now();
  if ((adminLooks.get(key) ?? 0) > now - 60 * 60 * 1000) {
    return;
  }
  if (adminLooks.size > 5000) {
    adminLooks.clear();
  }
  adminLooks.set(key, now);
  ctx.store.audit(ctx.user.id, 'document.admin_view', 'document', doc.id, { name: doc.name });
}

/** Owner and folder for something new: the caller's space, or a group's. */
function targetSpace(ctx, { groupId, folderId }) {
  const user = ctx.requireUser();
  const ownerKind = groupId ? 'group' : 'user';
  const ownerId = groupId ?? user.id;
  if (!atLeast(spaceRole(ctx.store, user, ownerKind, ownerId), 'editor')) {
    throw forbidden('You cannot add files there.');
  }
  if (folderId) {
    const folder = ctx.store.get('SELECT owner_kind, owner_id FROM folders WHERE id = ?', folderId);
    if (!folder || folder.owner_kind !== ownerKind || folder.owner_id !== ownerId) {
      throw badRequest('That folder is not in this space.');
    }
  }
  return { ownerKind, ownerId, folderId: folderId || null };
}

class Viewer {
  constructor(ctx) {
    this.ctx = ctx;
    this.users = new Map();
    this.groups = new Map();
    this.favorites = ctx.user
      ? new Set(ctx.store.all("SELECT target_id FROM favorites WHERE user_id = ? AND kind = 'document'", ctx.user.id).map((f) => f.target_id))
      : new Set();
  }

  user(id) {
    if (!id) {
      return null;
    }
    if (!this.users.has(id)) {
      this.users.set(id, publicUser(this.ctx.store.get('SELECT * FROM users WHERE id = ?', id)));
    }
    return this.users.get(id);
  }

  group(id) {
    if (!this.groups.has(id)) {
      const g = this.ctx.store.get('SELECT id, handle, name FROM groups WHERE id = ?', id);
      this.groups.set(id, g ? { id: g.id, handle: g.handle, name: g.name } : null);
    }
    return this.groups.get(id);
  }

  document(doc, role, adminView = false) {
    // A document without a preview of its latest version gets one, in the
    // background (a few at a time; the rest when next listed).
    if (doc.revision && doc.preview_revision !== doc.revision) {
      refreshPreview(this.ctx.app, doc.id);
    }
    const owner = doc.owner_kind === 'user' ? { kind: 'user', ...this.user(doc.owner_id) } : { kind: 'group', ...this.group(doc.owner_id) };
    return {
      id: doc.id,
      name: doc.name,
      description: doc.description,
      owner,
      folderId: doc.folder_id,
      visibility: doc.visibility,
      allowDownload: Boolean(doc.allow_download),
      allowDuplicate: Boolean(doc.allow_duplicate),
      revision: doc.revision,
      size: doc.size,
      thumbnailUrl: doc.thumbnail ? `/api/documents/${doc.id}/thumbnail?v=${doc.thumbnail.slice(0, 12)}` : null,
      previewUrl: doc.preview && doc.preview_revision === doc.revision ? `/api/documents/${doc.id}/preview?v=${doc.preview.slice(0, 12)}` : null,
      editing: this.editing(doc),
      info: JSON.parse(doc.info),
      createdAt: doc.created_at,
      modifiedAt: doc.modified_at,
      modifiedBy: this.user(doc.modified_by),
      trashedAt: doc.trashed_at,
      forkedFrom: doc.forked_from,
      role,
      canOpen: removedFor(this.ctx, doc) ? false : adminView || mayOpen(this.ctx.store, this.ctx.user, doc, role, this.ctx.linkToken),
      canDownload: removedFor(this.ctx, doc) ? false : mayDownload(this.ctx.store, this.ctx.user, doc, role, this.ctx.linkToken),
      adminView,
      // Its owner and the admins see what the moderators did.
      moderation: doc.moderation && (role === 'owner' || this.ctx.user?.is_admin)
        ? { state: doc.moderation, note: doc.moderation_note, at: doc.moderated_at } : null,
      favorite: this.favorites.has(doc.id),
    };
  }

  /** Someone else editing `doc` now (holding its edit lock), or null. */
  editing(doc) {
    const lock = this.ctx.store.get('SELECT user_id, expires_at FROM locks WHERE document_id = ?', doc.id);
    if (!lock || lock.expires_at < Date.now() || lock.user_id === this.ctx.user?.id) {
      return null;
    }
    return this.user(lock.user_id);
  }

  /** Where a document is, for search results: its space and folders. */
  where(doc) {
    const space = doc.owner_kind === 'group' ? this.group(doc.owner_id)?.name
      : (doc.owner_id === this.ctx.user?.id ? 'My Files' : this.user(doc.owner_id)?.name);
    return [space, ...folderPath(this.ctx.store, doc.folder_id).map((f) => f.name)].filter(Boolean).join(' / ');
  }

  /** Documents the caller can see, with their roles. */
  documents(rows) {
    const out = [];
    for (const doc of rows) {
      const role = documentRole(this.ctx.store, this.ctx.user, doc, { beta: this.ctx.settings.beta });
      if (role !== 'none') {
        out.push(this.document(doc, role));
      }
    }
    return out;
  }
}

function folderView(f) {
  return { id: f.id, name: f.name, parentId: f.parent_id, createdAt: f.created_at };
}

/** Folders from the space's root down to `folderId`. */
function folderPath(store, folderId) {
  const path = [];
  const seen = new Set();
  let id = folderId;
  while (id && !seen.has(id)) {
    seen.add(id);
    const f = store.get('SELECT * FROM folders WHERE id = ?', id);
    if (!f) {
      break;
    }
    path.unshift(folderView(f));
    id = f.parent_id;
  }
  return path;
}

// Documents shared with the user directly or through a group, owned by someone else.
const SHARED_SQL = `
  SELECT d.* FROM documents d WHERE d.trashed_at IS NULL
    AND NOT (d.owner_kind = 'user' AND d.owner_id = :u)
    AND d.id IN (SELECT document_id FROM shares WHERE
      (principal_kind = 'user' AND principal_id = :u) OR
      (principal_kind = 'group' AND principal_id IN (SELECT group_id FROM group_members WHERE user_id = :u)))
  ORDER BY d.modified_at DESC LIMIT ${LIST_LIMIT}`;

// Everything a user can reach by membership: theirs, their groups', shared.
const SEARCH_SQL = `
  SELECT d.* FROM documents d WHERE d.trashed_at IS NULL AND d.name LIKE :q ESCAPE '\\' AND (
    (d.owner_kind = 'user' AND d.owner_id = :u) OR
    (d.owner_kind = 'group' AND d.owner_id IN (SELECT group_id FROM group_members WHERE user_id = :u)) OR
    d.id IN (SELECT document_id FROM shares WHERE
      (principal_kind = 'user' AND principal_id = :u) OR
      (principal_kind = 'group' AND principal_id IN (SELECT group_id FROM group_members WHERE user_id = :u))))
  ORDER BY d.modified_at DESC LIMIT 100`;

/** `d` as an admin sees it: their own role if they have one, else a passive look. */
function passiveView(ctx, viewer, d) {
  const role = documentRole(ctx.store, ctx.user, d, { beta: ctx.settings.beta });
  return role === 'none' ? viewer.document(d, 'viewer', true) : viewer.document(d, role);
}

function list(ctx) {
  const user = ctx.requireUser();
  const { store } = ctx;
  const view = ctx.query('view') ?? 'files';
  const viewer = new Viewer(ctx);
  const q = ctx.query('q');
  if (q) {
    const like = `%${q.replace(/[\\%_]/g, (c) => `\\${c}`)}%`;
    const rows = store.all(SEARCH_SQL, { u: user.id, q: like });
    const byId = new Map(rows.map((r) => [r.id, r]));
    return { documents: viewer.documents(rows).map((d) => ({ ...d, where: viewer.where(byId.get(d.id)) })), folders: [] };
  }
  if (view === 'user') {
    // An account's documents, everywhere in their files and in the trash, for admins.
    ctx.requireAdmin();
    const rows = store.all(`SELECT * FROM documents WHERE owner_kind = 'user' AND owner_id = ? ORDER BY modified_at DESC LIMIT ${LIST_LIMIT}`, String(ctx.query('user') ?? ''));
    return { documents: rows.map((d) => passiveView(ctx, viewer, d)), folders: [] };
  }
  if (view === 'files' || view === 'group') {
    const ownerKind = view === 'group' ? 'group' : 'user';
    const ownerId = view === 'group' ? ctx.query('group') : user.id;
    let role = spaceRole(store, user, ownerKind, ownerId);
    // Admins look into any group's files, passively.
    const looking = role === 'none' && view === 'group' && user.is_admin && store.get('SELECT 1 FROM groups WHERE id = ?', ownerId);
    if (looking) {
      role = 'viewer';
    }
    if (role === 'none') {
      throw notFound('No such group, or you are not in it.');
    }
    const folderId = ctx.query('folder') || null;
    const docs = store.all(`SELECT * FROM documents WHERE owner_kind = ? AND owner_id = ? AND folder_id IS ? AND trashed_at IS NULL ORDER BY modified_at DESC LIMIT ${LIST_LIMIT}`, ownerKind, ownerId, folderId);
    const folders = store.all('SELECT * FROM folders WHERE owner_kind = ? AND owner_id = ? AND parent_id IS ? ORDER BY name COLLATE NOCASE', ownerKind, ownerId, folderId);
    return {
      documents: docs.map((d) => (looking ? passiveView(ctx, viewer, d) : viewer.document(d, documentRole(store, user, d, { beta: ctx.settings.beta })))),
      folders: folders.map(folderView),
      path: folderPath(store, folderId),
      canEdit: atLeast(role, 'editor'),
      usage: { bytes: usageBytes(store, ownerKind, ownerId), quota: quotaBytes(store, ownerKind, ownerId) },
    };
  }
  if (view === 'recent') {
    const rows = store.all(`SELECT d.* FROM recents r JOIN documents d ON d.id = r.document_id WHERE r.user_id = ? AND d.trashed_at IS NULL ORDER BY r.opened_at DESC LIMIT 60`, user.id);
    return { documents: viewer.documents(rows), folders: [] };
  }
  if (view === 'shared') {
    return { documents: viewer.documents(store.all(SHARED_SQL, { u: user.id })), folders: [] };
  }
  if (view === 'favorites') {
    const rows = store.all(`SELECT d.* FROM favorites f JOIN documents d ON d.id = f.target_id WHERE f.user_id = ? AND f.kind = 'document' AND d.trashed_at IS NULL ORDER BY f.created_at DESC`, user.id);
    return { documents: viewer.documents(rows), folders: [] };
  }
  if (view === 'trash') {
    const rows = store.all(
      `SELECT * FROM documents WHERE trashed_at IS NOT NULL AND (
         (owner_kind = 'user' AND owner_id = ?) OR
         (owner_kind = 'group' AND owner_id IN (SELECT group_id FROM group_members WHERE user_id = ? AND role = 'admin')))
       ORDER BY trashed_at DESC LIMIT ${LIST_LIMIT}`, user.id, user.id);
    return { documents: rows.map((d) => viewer.document(d, 'owner')), folders: [] };
  }
  throw badRequest(`Unknown view '${view}'.`);
}

function createDocument(ctx, space, name) {
  const user = ctx.user;
  const now = Date.now();
  const id = newId();
  ctx.store.run('INSERT INTO documents (id, owner_kind, owner_id, folder_id, name, created_by, created_at, modified_at, modified_by) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)',
    id, space.ownerKind, space.ownerId, space.folderId, name, user.id, now, now, user.id);
  touchRecent(ctx.store, user.id, id);
  return getDocument(ctx.store, id);
}

/** Whether `doc` was removed by the moderators, as far as the caller is concerned. */
function removedFor(ctx, doc) {
  return doc.moderation === 'removed' && !ctx.user?.is_admin;
}

function touchRecent(store, userId, documentId) {
  store.run('INSERT INTO recents (user_id, document_id, opened_at) VALUES (?, ?, ?) ON CONFLICT (user_id, document_id) DO UPDATE SET opened_at = excluded.opened_at',
    userId, documentId, Date.now());
}

function sharesView(ctx, doc, role) {
  const viewer = new Viewer(ctx);
  const rows = ctx.store.all('SELECT * FROM shares WHERE document_id = ? ORDER BY granted_at', doc.id);
  return {
    canManage: role === 'owner',
    visibility: doc.visibility,
    allowDownload: Boolean(doc.allow_download),
    allowDuplicate: Boolean(doc.allow_duplicate),
    owner: doc.owner_kind === 'user' ? { kind: 'user', ...viewer.user(doc.owner_id) } : { kind: 'group', ...viewer.group(doc.owner_id) },
    users: rows.filter((s) => s.principal_kind === 'user').map((s) => ({ user: viewer.user(s.principal_id), role: s.role, canDownload: Boolean(s.can_download), grantedAt: s.granted_at })),
    groups: rows.filter((s) => s.principal_kind === 'group').map((s) => ({ group: viewer.group(s.principal_id), role: s.role, canDownload: Boolean(s.can_download), grantedAt: s.granted_at })),
    links: role === 'owner'
      ? ctx.store.all('SELECT * FROM share_links WHERE document_id = ? AND revoked_at IS NULL ORDER BY created_at', doc.id)
        .map((l) => ({ id: l.id, role: l.role, createdAt: l.created_at, expiresAt: l.expires_at, createdBy: viewer.user(l.created_by) }))
      : [],
  };
}

function shareRole(value) {
  if (!SHARE_ROLES.includes(value)) {
    throw badRequest(`A role is one of ${SHARE_ROLES.join(', ')}.`);
  }
  return value;
}

/**
 * Saves `made` ({ file, thumbnail }: a .phx on disk, as the converter
 * leaves it) as the first revision of a new document in `space`, or makes
 * nothing. The file is deleted when the request ends.
 */
async function documentFrom(ctx, space, name, made) {
  ctx.temps.push(made.file);
  const doc = createDocument(ctx, space, name);
  try {
    await saveRevision(ctx.store, ctx.blobs, doc, made, { userId: ctx.user.id });
  } catch (err) {
    ctx.store.run('DELETE FROM documents WHERE id = ?', doc.id);
    throw err;
  }
  refreshPreview(ctx.app, doc.id);
  const saved = getDocument(ctx.store, doc.id);
  ctx.status = 201;
  return { document: new Viewer(ctx).document(saved, documentRole(ctx.store, ctx.user, saved, { beta: ctx.settings.beta })) };
}

export function registerDocuments(router, app) {
  router.get('/api/documents', (ctx) => list(ctx));

  // What a new document can start from: starter templates and tutorials.
  router.get('/api/starters', (ctx) => {
    ctx.requireUser();
    return app.starters.view();
  });

  router.get('/api/starters/:kind/:id/thumbnail', async (ctx) => {
    ctx.requireUser();
    const starter = ctx.params.kind === 'templates' ? app.starters.template(ctx.params.id)
      : ctx.params.kind === 'tutorials' ? app.starters.tutorial(ctx.params.id) : null;
    if (!starter?.thumbnail) {
      throw notFound('No such picture.');
    }
    const bytes = await fsp.readFile(starter.thumbnail);
    ctx.res.writeHead(200, {
      'Content-Type': starter.thumbnail.endsWith('.gif') ? 'image/gif' : 'image/png',
      'Content-Length': bytes.length,
      'Cache-Control': 'private, max-age=86400',
    });
    ctx.res.end(bytes);
    return undefined;
  });

  // A new document, made by Phoenix: blank ({ name, width, height, fps,
  // background }), a copy of a starter template ({ template }), or a
  // tutorial's starting document ({ tutorial }), which the editor then runs
  // the lesson on.
  // Each person may have Phoenix make so many documents an hour; admins any number.
  const chargeConversion = (ctx) => {
    if (!ctx.user.is_admin && !app.conversions.take(ctx.user.id)) {
      throw new HttpError(429, 'slow_down', `Phoenix has made ${ctx.config.conversionsPerHour} documents for you in the last hour, the most it makes in an hour. Try again a little later.`);
    }
  };

  router.post('/api/documents', async (ctx) => {
    const body = await ctx.json();
    const space = targetSpace(ctx, { groupId: body.groupId, folderId: body.folderId });
    chargeConversion(ctx);
    if (body.template) {
      const template = app.starters.template(String(body.template));
      if (!template) {
        throw notFound('No such template.');
      }
      const thumbnail = template.thumbnail ? await fsp.readFile(template.thumbnail) : undefined;
      const made = await app.converter.fromFile(template.document, thumbnail);
      return documentFrom(ctx, space, cleanName(body.name, template.title), made);
    }
    if (body.tutorial) {
      const lesson = app.starters.tutorial(String(body.tutorial));
      if (!lesson) {
        throw notFound('No such tutorial.');
      }
      const made = lesson.document
        ? await app.converter.fromFile(lesson.document)
        : await app.converter.newDocument({ ...lesson.stage, background: '#ffffff' });
      return { ...(await documentFrom(ctx, space, cleanName(body.name, lesson.title), made)), tutorial: lesson.id };
    }
    const background = String(body.background ?? '#ffffff');
    if (!/^#[0-9a-f]{6}$/i.test(background)) {
      throw badRequest('The background is a color like #ffffff.');
    }
    const made = await app.converter.newDocument({
      width: Math.round(wholeNumber(body.width ?? 1920, 1, 8192, 'The width')),
      height: Math.round(wholeNumber(body.height ?? 1080, 1, 8192, 'The height')),
      fps: wholeNumber(body.fps ?? 24, 1, 120, 'The frame rate'),
      background,
    });
    return documentFrom(ctx, space, cleanName(body.name), made);
  });

  // The body is a file: a .phx, or an FLA, SWF, SVG, EPS or AI that Phoenix
  // turns into one. ?name= (its file name), ?folderId=, ?groupId=. It goes
  // to a file on disk as it arrives, never into memory whole.
  router.post('/api/documents/upload', async (ctx) => {
    const space = targetSpace(ctx, { groupId: ctx.query('groupId'), folderId: ctx.query('folderId') });
    const ext = extensionOf(ctx.query('name')) || 'phx';
    if (ext !== 'phx' && UPLOAD_KINDS[ext]) {
      chargeConversion(ctx);
    }
    const limit = ext === 'phx' ? ctx.config.maxUploadBytes : Math.min(ctx.config.maxUploadBytes, ctx.config.convertMaxUploadBytes);
    const { file } = await ctx.upload(/^[a-z0-9]{1,5}$/.test(ext) ? ext : 'bin', limit);
    const made = await app.converter.fromUpload(file, ext);
    return documentFrom(ctx, space, nameFromFile(ctx.query('name')), made);
  });

  router.get('/api/documents/:id', (ctx) => {
    const { doc, role, adminView } = access(ctx, ctx.params.id, 'viewer', { removedOk: true });
    if (adminView) {
      noteAdminView(ctx, doc);
    }
    return { document: new Viewer(ctx).document(doc, role, adminView) };
  });

  router.patch('/api/documents/:id', async (ctx) => {
    const { doc, role } = access(ctx, ctx.params.id, 'editor');
    const body = await ctx.json();
    const { store } = ctx;
    const ownerOnly = ['visibility', 'allowDownload', 'allowDuplicate'].filter((k) => body[k] !== undefined);
    if (ownerOnly.length && role !== 'owner') {
      throw forbidden('Only its owner can change who sees it.');
    }
    if (body.name !== undefined) {
      store.run('UPDATE documents SET name = ? WHERE id = ?', cleanName(body.name), doc.id);
    }
    if (typeof body.description === 'string') {
      store.run('UPDATE documents SET description = ? WHERE id = ?', body.description.slice(0, 2000), doc.id);
    }
    if (body.folderId !== undefined) {
      if (!atLeast(spaceRole(store, ctx.user, doc.owner_kind, doc.owner_id), 'editor')) {
        throw forbidden('Only people who can edit its space can move it.');
      }
      if (body.folderId !== null) {
        const folder = store.get('SELECT * FROM folders WHERE id = ?', body.folderId);
        if (!folder || folder.owner_kind !== doc.owner_kind || folder.owner_id !== doc.owner_id) {
          throw badRequest('That folder is not in the same space.');
        }
      }
      store.run('UPDATE documents SET folder_id = ? WHERE id = ?', body.folderId, doc.id);
    }
    if (body.visibility !== undefined) {
      if (!VISIBILITIES.includes(body.visibility)) {
        throw badRequest(`Visibility is one of ${VISIBILITIES.join(', ')}.`);
      }
      store.run('UPDATE documents SET visibility = ? WHERE id = ?', body.visibility, doc.id);
      store.audit(ctx.user.id, 'document.visibility', 'document', doc.id, { from: doc.visibility, to: body.visibility });
    }
    if (body.allowDownload !== undefined) {
      store.run('UPDATE documents SET allow_download = ? WHERE id = ?', Boolean(body.allowDownload), doc.id);
    }
    if (body.allowDuplicate !== undefined) {
      store.run('UPDATE documents SET allow_duplicate = ? WHERE id = ?', Boolean(body.allowDuplicate), doc.id);
    }
    const updated = getDocument(store, doc.id);
    return { document: new Viewer(ctx).document(updated, role) };
  });

  // To the trash; ?purge=1 deletes a trashed document for good.
  router.delete('/api/documents/:id', (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'owner', { removedOk: true });
    if (ctx.query('purge') === '1') {
      if (!doc.trashed_at) {
        throw badRequest('Move it to the trash first.');
      }
      ctx.store.run('DELETE FROM documents WHERE id = ?', doc.id);
      ctx.store.audit(ctx.user.id, 'document.purge', 'document', doc.id, { name: doc.name });
      return { ok: true };
    }
    ctx.store.run('UPDATE documents SET trashed_at = ? WHERE id = ?', Date.now(), doc.id);
    return { ok: true };
  });

  router.post('/api/documents/:id/restore', (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'owner', { removedOk: true });
    ctx.store.run('UPDATE documents SET trashed_at = NULL WHERE id = ?', doc.id);
    return { ok: true };
  });

  router.post('/api/documents/:id/duplicate', async (ctx) => {
    const { doc, role } = access(ctx, ctx.params.id);
    if (!atLeast(role, 'editor') && !doc.allow_duplicate) {
      throw forbidden('Its owner does not allow copies.');
    }
    // A copy of one's own is as good as a download.
    if (!mayDownload(ctx.store, ctx.user, doc, role, ctx.linkToken)) {
      throw forbidden('You do not have permission to download this document, so you cannot copy it.');
    }
    const body = await ctx.json();
    const space = targetSpace(ctx, { groupId: body.groupId, folderId: body.folderId });
    const copy = createDocument(ctx, space, cleanName(body.name, `${doc.name} copy`));
    ctx.store.run('UPDATE documents SET forked_from = ?, description = ? WHERE id = ?', doc.id, doc.description, copy.id);
    if (doc.revision > 0) {
      try {
        copyRevision(ctx.store, doc, doc.revision, copy, { userId: ctx.user.id });
        refreshPreview(app, copy.id);
      } catch (err) {
        ctx.store.run('DELETE FROM documents WHERE id = ?', copy.id);
        throw err;
      }
    }
    ctx.status = 201;
    return { document: new Viewer(ctx).document(getDocument(ctx.store, copy.id), 'owner') };
  });

  router.post('/api/documents/:id/opened', (ctx) => {
    const user = ctx.requireUser();
    const { doc, adminView } = access(ctx, ctx.params.id);
    // An admin's look is passive: not in their recents, not a view.
    if (!adminView) {
      touchRecent(ctx.store, user.id, doc.id);
    }
    return { ok: true };
  });

  // The file for the web editor: editors, and anyone who may download it.
  router.get('/api/documents/:id/file', async (ctx) => {
    const { doc, role } = access(ctx, ctx.params.id);
    if (!canOpen(ctx, doc, role)) {
      throw forbidden('You cannot open this document in the editor.');
    }
    return sendRevision(ctx, doc);
  });

  // A copy to keep: only for people who may download it.
  router.get('/api/documents/:id/download', async (ctx) => {
    const { doc, role } = access(ctx, ctx.params.id);
    if (!mayDownload(ctx.store, ctx.user, doc, role, ctx.linkToken)) {
      throw forbidden('You do not have permission to download this document.');
    }
    ctx.store.audit(ctx.user?.id ?? null, 'document.download', 'document', doc.id, {});
    return sendRevision(ctx, doc);
  });

  // Several documents in one ZIP (?ids=a,b,c), each at its latest version.
  router.get('/api/download', async (ctx) => {
    const user = ctx.requireUser();
    const ids = [...new Set(String(ctx.query('ids') ?? '').split(',').filter(Boolean))];
    if (!ids.length || ids.length > MAX_DOWNLOAD) {
      throw badRequest(`Choose from 1 to ${MAX_DOWNLOAD} documents.`);
    }
    const taken = new Set();
    const entries = [];
    for (const id of ids) {
      const { doc, role } = access(ctx, id);
      if (!mayDownload(ctx.store, user, doc, role, null)) {
        throw forbidden(`You do not have permission to download “${doc.name}”.`);
      }
      if (!doc.revision) {
        continue;
      }
      const file = revisionFile(ctx.store, doc, doc.revision);
      // Names are unique in the ZIP: "Walk.phx", "Walk (2).phx".
      const base = doc.name.replace(/["\\/:*?<>|\u0000-\u001f]+/g, '_') || 'Untitled';
      let name = `${base}.phx`;
      for (let n = 2; taken.has(name.toLowerCase()); n++) {
        name = `${base} (${n}).phx`;
      }
      taken.add(name.toLowerCase());
      entries.push({ name, size: file.size, modifiedAt: doc.modified_at, bytes: () => fileBytes(ctx.blobs, file) });
    }
    if (!entries.length) {
      throw notFound('Those documents have nothing saved yet.');
    }
    const size = zipSize(entries);
    if (size > ZIP_LIMIT) {
      throw new HttpError(413, 'too_large', 'That is more than 4 GB; download fewer at a time.');
    }
    for (const id of ids) {
      ctx.store.audit(user.id, 'document.download', 'document', id, { zip: true });
    }
    ctx.res.writeHead(200, {
      'Content-Type': 'application/zip',
      'Content-Length': size,
      'Content-Disposition': 'attachment; filename="Phoenix documents.zip"',
      'Cache-Control': 'private, no-store',
    });
    if (ctx.req.method === 'HEAD') {
      ctx.res.end();
      return undefined;
    }
    await writeZip(entries, ctx.res);
    return undefined;
  });

  /** Sends `doc`'s file at ?rev= (else its latest version). */
  async function sendRevision(ctx, doc) {
    const number = ctx.query('rev') ? Number(ctx.query('rev')) : doc.revision;
    if (!Number.isInteger(number) || number < 1) {
      throw notFound('This document has nothing saved yet.');
    }
    const file = revisionFile(ctx.store, doc, number);
    const filename = `${doc.name.replace(/["\\/:*?<>|]+/g, '_')}.phx`;
    ctx.res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': file.size,
      'Content-Disposition': `attachment; filename="${filename.replace(/[^\x20-\x7e]/g, '_')}"; filename*=UTF-8''${encodeURIComponent(filename)}`,
      'Cache-Control': 'private, no-store',
      'X-Phoenix-Revision': String(number),
    });
    if (ctx.req.method === 'HEAD') {
      ctx.res.end();
      return undefined;
    }
    // Rebuilt as it is sent, a stored chunk at a time.
    await sendFile(ctx.blobs, file, ctx.res);
    return undefined;
  }

  // The body is the whole .phx, saved to a file as it arrives. ?base= is the
  // revision it was opened at.
  router.put('/api/documents/:id/file', async (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'editor');
    const base = ctx.query('base') !== null ? Number(ctx.query('base')) : null;
    if (base !== null && !Number.isInteger(base)) {
      throw badRequest('base is a revision number.');
    }
    const lock = ctx.store.get('SELECT user_id, expires_at FROM locks WHERE document_id = ?', doc.id);
    if (lock && lock.user_id !== ctx.user.id && lock.expires_at > Date.now()) {
      const holder = ctx.store.get('SELECT name FROM users WHERE id = ?', lock.user_id);
      ctx.req.resume();
      throw new HttpError(423, 'locked', `${holder?.name ?? 'Someone'} is editing this document, so it can't be saved over now.`);
    }
    const { file } = await ctx.upload('phx');
    const number = await saveRevision(ctx.store, ctx.blobs, doc, { file }, { userId: ctx.user.id, base, label: ctx.query('label') ?? '' });
    touchRecent(ctx.store, ctx.user.id, doc.id);
    // Saved: the draft of it is no longer needed.
    deleteDraft(ctx.store, doc.id, ctx.user.id);
    refreshPreview(app, doc.id);
    return { revision: number };
  });

  router.get('/api/documents/:id/preview', async (ctx) => {
    const { doc } = access(ctx, ctx.params.id);
    if (!doc.preview) {
      throw notFound('No preview.');
    }
    const picture = await ctx.blobs.get(doc.preview);
    const type = picture.subarray(0, 4).toString('latin1') === 'GIF8' ? 'image/gif' : 'image/webp';
    ctx.res.writeHead(200, { 'Content-Type': type, 'Content-Length': picture.length, 'Cache-Control': 'private, max-age=31536000, immutable' });
    ctx.res.end(picture);
    return undefined;
  });

  router.get('/api/documents/:id/revisions/:number/thumbnail', async (ctx) => {
    const { doc } = access(ctx, ctx.params.id);
    const rev = ctx.store.get('SELECT manifest FROM revisions WHERE document_id = ? AND number = ?', doc.id, Number(ctx.params.number));
    const thumb = rev && JSON.parse(rev.manifest).chunks.find((c) => c.t === 'THMB' && (c.f & 0xf) === 0);
    if (!thumb) {
      throw notFound('No thumbnail.');
    }
    const png = await ctx.blobs.get(thumb.h);
    ctx.res.writeHead(200, { 'Content-Type': 'image/png', 'Content-Length': png.length, 'Cache-Control': 'private, max-age=31536000, immutable' });
    ctx.res.end(png);
    return undefined;
  });

  // How to play a document: in the web editor for people who may open it,
  // else as an HTML5 bundle on the play origin (made by Phoenix, once a
  // revision, which can take a moment).
  router.get('/api/documents/:id/play', async (ctx) => {
    const { doc, role } = access(ctx, ctx.params.id);
    if (!doc.revision) {
      throw notFound('This document has nothing saved yet.');
    }
    if (canOpen(ctx, doc, role) && editorAvailable(ctx.config) && ctx.query('as') !== 'bundle') {
      return { mode: 'editor', url: `/editor/?doc=${doc.id}&play=1` };
    }
    await ensureBundle(app, doc);
    return { mode: 'bundle', url: bundleUrl(app, doc), width: JSON.parse(doc.info).width ?? null, height: JSON.parse(doc.info).height ?? null };
  });

  // ── Edit locks: one person edits at a time ───────────────────────────────

  // Takes or keeps the lock (the editor asks every half minute); someone
  // else holding it is answered with who.
  router.post('/api/documents/:id/lock', (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'editor');
    const now = Date.now();
    const lock = ctx.store.get('SELECT user_id, expires_at FROM locks WHERE document_id = ?', doc.id);
    if (lock && lock.user_id !== ctx.user.id && lock.expires_at > now) {
      return { held: false, holder: publicUser(ctx.store.get('SELECT * FROM users WHERE id = ?', lock.user_id)) };
    }
    ctx.store.run(`INSERT INTO locks (document_id, user_id, expires_at) VALUES (?, ?, ?)
                   ON CONFLICT (document_id) DO UPDATE SET user_id = excluded.user_id, expires_at = excluded.expires_at`,
    doc.id, ctx.user.id, now + LOCK_MS);
    return { held: true };
  });

  router.delete('/api/documents/:id/lock', (ctx) => {
    const user = ctx.requireUser();
    ctx.store.run('DELETE FROM locks WHERE document_id = ? AND user_id = ?', ctx.params.id, user.id);
    return { ok: true };
  });

  // ── Drafts: each person's unsaved work, kept by the editor ───────────────

  router.put('/api/documents/:id/draft', async (ctx) => {
    const { doc, role, adminView } = access(ctx, ctx.params.id);
    if (adminView) {
      throw forbidden('An admin\'s look at a document keeps no draft.');
    }
    if (!mayOpen(ctx.store, ctx.user, doc, role, ctx.linkToken)) {
      throw forbidden('You cannot open this document in the editor.');
    }
    const { file } = await ctx.upload('phx');
    return saveDraft(ctx.store, ctx.blobs, doc, ctx.user.id, file);
  });

  router.get('/api/documents/:id/draft', (ctx) => {
    const { doc } = access(ctx, ctx.params.id);
    const draft = draftOf(ctx.store, doc, ctx.user.id);
    // None is an ordinary answer (the editor asks every time it opens).
    return { draft: draft ? { savedAt: draft.savedAt, baseRevision: draft.baseRevision, size: draft.size } : null };
  });

  router.get('/api/documents/:id/draft/file', async (ctx) => {
    const { doc } = access(ctx, ctx.params.id);
    const draft = draftOf(ctx.store, doc, ctx.user.id);
    if (!draft) {
      throw notFound('No draft.');
    }
    ctx.res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': draft.file.size, 'Cache-Control': 'private, no-store' });
    await sendFile(ctx.blobs, draft.file, ctx.res);
    return undefined;
  });

  router.delete('/api/documents/:id/draft', (ctx) => {
    const user = ctx.requireUser();
    deleteDraft(ctx.store, ctx.params.id, user.id);
    return { ok: true };
  });

  router.get('/api/documents/:id/thumbnail', async (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'viewer', { removedOk: true });
    if (!doc.thumbnail) {
      throw notFound('No thumbnail.');
    }
    const png = await ctx.blobs.get(doc.thumbnail);
    ctx.res.writeHead(200, { 'Content-Type': 'image/png', 'Content-Length': png.length, 'Cache-Control': 'private, max-age=31536000, immutable' });
    ctx.res.end(png);
    return undefined;
  });

  router.get('/api/documents/:id/revisions', (ctx) => {
    const { doc } = access(ctx, ctx.params.id);
    const viewer = new Viewer(ctx);
    const rows = ctx.store.all('SELECT number, created_by, created_at, label, size, application, manifest FROM revisions WHERE document_id = ? ORDER BY number DESC', doc.id);
    return {
      revisions: rows.map((r) => {
        const thumb = JSON.parse(r.manifest).chunks.find((c) => c.t === 'THMB' && (c.f & 0xf) === 0);
        return {
          number: r.number, createdBy: viewer.user(r.created_by), createdAt: r.created_at, label: r.label, size: r.size, application: r.application,
          thumbnailUrl: thumb ? `/api/documents/${doc.id}/revisions/${r.number}/thumbnail?v=${thumb.h.slice(0, 12)}` : null,
        };
      }),
    };
  });

  router.patch('/api/documents/:id/revisions/:number', async (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'editor');
    const body = await ctx.json();
    const { changes } = ctx.store.run('UPDATE revisions SET label = ? WHERE document_id = ? AND number = ?', String(body.label ?? '').slice(0, 120), doc.id, Number(ctx.params.number));
    if (!changes) {
      throw notFound('No such revision.');
    }
    return { ok: true };
  });

  router.post('/api/documents/:id/revisions/:number/restore', (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'editor');
    const number = Number(ctx.params.number);
    const made = copyRevision(ctx.store, doc, number, doc, { userId: ctx.user.id, label: `Restored from ${number}` });
    refreshPreview(app, doc.id);
    return { revision: made };
  });

  // ── Sharing ─────────────────────────────────────────────────────────────

  router.get('/api/documents/:id/shares', (ctx) => {
    const { doc, role } = access(ctx, ctx.params.id, 'editor');
    return sharesView(ctx, doc, role);
  });

  router.post('/api/documents/:id/shares', async (ctx) => {
    const { doc, role } = access(ctx, ctx.params.id, 'owner');
    const body = await ctx.json();
    const r = shareRole(body.role);
    const { store } = ctx;
    let kind;
    let principalId;
    if (body.kind === 'group') {
      const group = store.get('SELECT * FROM groups WHERE id = ? OR handle = ?', String(body.who ?? ''), String(body.who ?? '').replace(/^@/, ''));
      if (!group) {
        throw notFound('No such group.');
      }
      if (!groupRole(store, ctx.user.id, group.id) && !ctx.user.is_admin) {
        throw forbidden('You can share only with groups you are in.');
      }
      if (doc.owner_kind === 'group' && doc.owner_id === group.id) {
        throw badRequest('That group owns it already.');
      }
      kind = 'group';
      principalId = group.id;
    } else {
      const user = findUser(store, body.who);
      if (!user) {
        throw notFound('No one with an account has that user name or email. They can register, or an admin can make an account for them.');
      }
      if (doc.owner_kind === 'user' && doc.owner_id === user.id) {
        throw badRequest('They own it.');
      }
      kind = 'user';
      principalId = user.id;
    }
    // Editors can download unless the owner says otherwise; viewers only when they say so.
    const download = body.canDownload === undefined ? r === 'editor' : Boolean(body.canDownload);
    store.run(`INSERT INTO shares (document_id, principal_kind, principal_id, role, can_download, granted_by, granted_at) VALUES (?, ?, ?, ?, ?, ?, ?)
               ON CONFLICT (document_id, principal_kind, principal_id) DO UPDATE SET role = excluded.role, can_download = excluded.can_download,
                 granted_by = excluded.granted_by, granted_at = excluded.granted_at`,
    doc.id, kind, principalId, r, download ? 1 : 0, ctx.user.id, Date.now());
    store.audit(ctx.user.id, 'share.set', 'document', doc.id, { kind, principalId, role: r, canDownload: download });
    const told = kind === 'user' ? [principalId] : store.all('SELECT user_id FROM group_members WHERE group_id = ?', principalId).map((m) => m.user_id);
    notify(app, told, { kind: 'share', text: `${ctx.user.name} shared “${doc.name}” with you`, link: `/d/${doc.id}`, actorId: ctx.user.id });
    return sharesView(ctx, doc, role);
  });

  router.patch('/api/documents/:id/shares/:kind/:principal', async (ctx) => {
    const { doc, role } = access(ctx, ctx.params.id, 'owner');
    const body = await ctx.json();
    const share = ctx.store.get('SELECT * FROM shares WHERE document_id = ? AND principal_kind = ? AND principal_id = ?', doc.id, ctx.params.kind, ctx.params.principal);
    if (!share) {
      throw notFound('No such share.');
    }
    // Either or both: its role, whether it can download.
    const r = body.role === undefined ? share.role : shareRole(body.role);
    const download = body.canDownload === undefined ? Boolean(share.can_download) : Boolean(body.canDownload);
    ctx.store.run('UPDATE shares SET role = ?, can_download = ? WHERE document_id = ? AND principal_kind = ? AND principal_id = ?',
      r, download ? 1 : 0, doc.id, ctx.params.kind, ctx.params.principal);
    ctx.store.audit(ctx.user.id, 'share.set', 'document', doc.id, { kind: ctx.params.kind, principalId: ctx.params.principal, role: r, canDownload: download });
    return sharesView(ctx, doc, role);
  });

  // The owner hands the document to another person, or to a group they are
  // in: { kind: 'user' | 'group', to }. They go on as an editor of it.
  router.post('/api/documents/:id/transfer', async (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'owner');
    const body = await ctx.json();
    const { store } = ctx;
    let to;
    let told;
    let toName;
    if (body.kind === 'group') {
      const group = store.get('SELECT * FROM groups WHERE id = ? OR handle = ?', String(body.to ?? ''), String(body.to ?? '').replace(/^@/, ''));
      if (!group || !groupRole(store, ctx.user.id, group.id)) {
        throw notFound('No such group, or you are not in it.');
      }
      to = { kind: 'group', id: group.id };
      told = group.owner_id ? [group.owner_id] : [];
      toName = group.name;
    } else {
      const user = findUser(store, body.to);
      if (!user) {
        throw notFound('No one with an account has that name or email.');
      }
      to = { kind: 'user', id: user.id };
      told = [user.id];
      toName = user.name;
    }
    transferDocument(store, doc, to, ctx.user.id);
    notify(app, told.filter((id) => id !== ctx.user.id), {
      kind: 'transfer', text: `${ctx.user.name} gave ${to.kind === 'group' ? toName : 'you'} “${doc.name}”`, link: `/d/${doc.id}`, actorId: ctx.user.id,
    });
    return { ok: true, owner: to };
  });

  // The owner removes a share; anyone may remove their own (leave it).
  router.delete('/api/documents/:id/shares/:kind/:principal', (ctx) => {
    const user = ctx.requireUser();
    const { doc, role } = access(ctx, ctx.params.id);
    const own = ctx.params.kind === 'user' && ctx.params.principal === user.id;
    if (role !== 'owner' && !own) {
      throw forbidden('Only its owner can change who it is shared with.');
    }
    const { changes } = ctx.store.run('DELETE FROM shares WHERE document_id = ? AND principal_kind = ? AND principal_id = ?', doc.id, ctx.params.kind, ctx.params.principal);
    if (!changes) {
      throw notFound('No such share.');
    }
    ctx.store.audit(user.id, 'share.remove', 'document', doc.id, { kind: ctx.params.kind, principalId: ctx.params.principal });
    return own && role !== 'owner' ? { ok: true } : sharesView(ctx, doc, role);
  });

  router.post('/api/documents/:id/links', async (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'owner');
    const body = await ctx.json();
    const r = shareRole(body.role ?? 'viewer');
    const days = body.days === undefined || body.days === null ? null : Number(body.days);
    if (days !== null && (!Number.isInteger(days) || days < 1 || days > 365)) {
      throw badRequest('A link lasts 1 to 365 days, or forever.');
    }
    const token = newToken();
    const id = newId();
    const now = Date.now();
    ctx.store.run('INSERT INTO share_links (id, document_id, token_hash, role, created_by, created_at, expires_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
      id, doc.id, hashToken(token), r, ctx.user.id, now, days ? now + days * 86400000 : null);
    ctx.store.audit(ctx.user.id, 'link.create', 'document', doc.id, { role: r, days });
    ctx.status = 201;
    // The token is shown once; only its hash is kept.
    return { id, role: r, url: `${ctx.config.publicUrl}/d/${doc.id}?link=${token}` };
  });

  router.delete('/api/documents/:id/links/:link', (ctx) => {
    const { doc } = access(ctx, ctx.params.id, 'owner');
    const { changes } = ctx.store.run('UPDATE share_links SET revoked_at = ? WHERE id = ? AND document_id = ? AND revoked_at IS NULL', Date.now(), ctx.params.link, doc.id);
    if (!changes) {
      throw notFound('No such link.');
    }
    ctx.store.audit(ctx.user.id, 'link.revoke', 'document', doc.id, { link: ctx.params.link });
    return { ok: true };
  });

  // ── Folders ─────────────────────────────────────────────────────────────

  // Every folder in a space (yours, or ?group='s), for choosing where to move things.
  router.get('/api/folders', (ctx) => {
    const user = ctx.requireUser();
    const groupId = ctx.query('group');
    const ownerKind = groupId ? 'group' : 'user';
    const ownerId = groupId ?? user.id;
    if (spaceRole(ctx.store, user, ownerKind, ownerId) === 'none') {
      throw notFound('No such group, or you are not in it.');
    }
    return { folders: ctx.store.all('SELECT * FROM folders WHERE owner_kind = ? AND owner_id = ? ORDER BY name COLLATE NOCASE', ownerKind, ownerId).map(folderView) };
  });

  router.post('/api/folders', async (ctx) => {
    const body = await ctx.json();
    const space = targetSpace(ctx, { groupId: body.groupId, folderId: body.parentId });
    const id = newId();
    ctx.store.run('INSERT INTO folders (id, owner_kind, owner_id, parent_id, name, created_by, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
      id, space.ownerKind, space.ownerId, space.folderId, cleanName(body.name, 'New folder'), ctx.user.id, Date.now());
    ctx.status = 201;
    return { folder: folderView(ctx.store.get('SELECT * FROM folders WHERE id = ?', id)) };
  });

  const folderAccess = (ctx) => {
    const user = ctx.requireUser();
    const folder = ctx.store.get('SELECT * FROM folders WHERE id = ?', ctx.params.id);
    const role = folder ? spaceRole(ctx.store, user, folder.owner_kind, folder.owner_id) : 'none';
    if (role === 'none') {
      throw notFound('No such folder.');
    }
    if (!atLeast(role, 'editor')) {
      throw forbidden('You cannot change folders here.');
    }
    return folder;
  };

  router.patch('/api/folders/:id', async (ctx) => {
    const folder = folderAccess(ctx);
    const body = await ctx.json();
    if (body.name !== undefined) {
      ctx.store.run('UPDATE folders SET name = ? WHERE id = ?', cleanName(body.name, folder.name), folder.id);
    }
    if (body.parentId !== undefined) {
      if (body.parentId !== null) {
        const parent = ctx.store.get('SELECT * FROM folders WHERE id = ?', body.parentId);
        if (!parent || parent.owner_kind !== folder.owner_kind || parent.owner_id !== folder.owner_id) {
          throw badRequest('That folder is not in the same space.');
        }
        if (folderPath(ctx.store, parent.id).some((f) => f.id === folder.id)) {
          throw badRequest('A folder cannot go inside itself.');
        }
      }
      ctx.store.run('UPDATE folders SET parent_id = ? WHERE id = ?', body.parentId, folder.id);
    }
    return { folder: folderView(ctx.store.get('SELECT * FROM folders WHERE id = ?', folder.id)) };
  });

  // Deletes the folder and its subfolders; their documents go to the trash.
  router.delete('/api/folders/:id', (ctx) => {
    const folder = folderAccess(ctx);
    ctx.store.transaction(() => {
      ctx.store.run(`WITH RECURSIVE sub (id) AS (SELECT ? UNION ALL SELECT f.id FROM folders f JOIN sub ON f.parent_id = sub.id)
                     UPDATE documents SET trashed_at = ? WHERE folder_id IN sub AND trashed_at IS NULL`, folder.id, Date.now());
      ctx.store.run('DELETE FROM folders WHERE id = ?', folder.id);
    });
    return { ok: true };
  });

  // ── Favorites ───────────────────────────────────────────────────────────

  router.put('/api/favorites/:kind/:id', (ctx) => {
    const user = ctx.requireUser();
    if (ctx.params.kind === 'document') {
      access(ctx, ctx.params.id);
    } else if (ctx.params.kind === 'group') {
      if (!groupRole(ctx.store, user.id, ctx.params.id)) {
        throw notFound('No such group.');
      }
    } else {
      throw badRequest('Favorites are documents or groups.');
    }
    ctx.store.run('INSERT OR IGNORE INTO favorites (user_id, kind, target_id, created_at) VALUES (?, ?, ?, ?)', user.id, ctx.params.kind, ctx.params.id, Date.now());
    return { ok: true };
  });

  router.delete('/api/favorites/:kind/:id', (ctx) => {
    const user = ctx.requireUser();
    ctx.store.run('DELETE FROM favorites WHERE user_id = ? AND kind = ? AND target_id = ?', user.id, ctx.params.kind, ctx.params.id);
    return { ok: true };
  });

  // ── Moderation ──────────────────────────────────────────────────────────

  // Anyone who can see a document (but not its owner) reports it to the admins: { reason }.
  router.post('/api/documents/:id/report', async (ctx) => {
    const user = ctx.requireUser();
    const { doc, role } = access(ctx, ctx.params.id);
    if (role === 'owner') {
      throw badRequest('That is your own document.');
    }
    if (!ctx.store.get('SELECT 1 FROM document_reports WHERE document_id = ? AND reporter_id = ? AND resolved_at IS NULL', doc.id, user.id)) {
      const body = await ctx.json();
      ctx.store.run('INSERT INTO document_reports (document_id, reporter_id, reason, created_at) VALUES (?, ?, ?, ?)',
        doc.id, user.id, String(body.reason ?? '').trim().slice(0, 1000), Date.now());
      notify(app, adminIds(ctx.store), { kind: 'report', text: `${user.name} reported “${doc.name}”`, link: '/admin/reports', actorId: user.id });
    }
    ctx.status = 201;
    return { ok: true };
  });

  router.get('/api/admin/document-reports', (ctx) => {
    ctx.requireAdmin();
    const all = ctx.query('all') === '1';
    const viewer = new Viewer(ctx);
    const rows = ctx.store.all(
      `SELECT r.*, d.id AS doc_id FROM document_reports r JOIN documents d ON d.id = r.document_id
       ${all ? '' : 'WHERE r.resolved_at IS NULL'} ORDER BY r.created_at DESC LIMIT 200`);
    return {
      reports: rows.map((r) => ({
        id: r.id,
        reason: r.reason,
        createdAt: r.created_at,
        reporter: viewer.user(r.reporter_id),
        resolvedAt: r.resolved_at,
        outcome: r.outcome,
        document: passiveView(ctx, viewer, getDocument(ctx.store, r.doc_id)),
      })),
    };
  });

  // { outcome: 'dismissed' | 'flagged' | 'removed', note }: the last two moderate the document.
  router.patch('/api/admin/document-reports/:id', async (ctx) => {
    const admin = ctx.requireAdmin();
    const report = ctx.store.get('SELECT * FROM document_reports WHERE id = ?', Number(ctx.params.id));
    if (!report) {
      throw notFound('No such report.');
    }
    const body = await ctx.json();
    if (!['dismissed', 'flagged', 'removed'].includes(body.outcome)) {
      throw badRequest('A report is dismissed, or its document flagged or removed.');
    }
    if (body.outcome !== 'dismissed') {
      moderate(ctx, getDocument(ctx.store, report.document_id), body.outcome, body.note);
    } else {
      ctx.store.run('UPDATE document_reports SET resolved_at = ?, resolved_by = ?, outcome = ? WHERE document_id = ? AND resolved_at IS NULL',
        Date.now(), admin.id, 'dismissed', report.document_id);
    }
    return { ok: true };
  });

  // { state: null | 'flagged' | 'removed', note }: what the admins say of a document.
  router.patch('/api/admin/documents/:id/moderation', async (ctx) => {
    ctx.requireAdmin();
    const doc = getDocument(ctx.store, ctx.params.id);
    if (!doc) {
      throw notFound('No such document.');
    }
    const body = await ctx.json();
    if (![null, 'flagged', 'removed'].includes(body.state ?? null)) {
      throw badRequest('A document is flagged, removed, or neither.');
    }
    moderate(ctx, doc, body.state ?? null, body.note);
    return { document: new Viewer(ctx).document(getDocument(ctx.store, doc.id), 'viewer', true) };
  });

  /** Sets `doc`'s moderation, settles its reports, and tells its owner. */
  function moderate(ctx, doc, state, note) {
    const text = String(note ?? '').trim().slice(0, 500);
    const { store } = ctx;
    const now = Date.now();
    store.transaction(() => {
      store.run('UPDATE documents SET moderation = ?, moderation_note = ?, moderated_by = ?, moderated_at = ? WHERE id = ?',
        state, state ? text : '', ctx.user.id, now, doc.id);
      if (state) {
        store.run('UPDATE document_reports SET resolved_at = ?, resolved_by = ?, outcome = ? WHERE document_id = ? AND resolved_at IS NULL',
          now, ctx.user.id, state, doc.id);
      }
      // Removed: no one goes on editing it.
      if (state === 'removed') {
        store.run('DELETE FROM locks WHERE document_id = ?', doc.id);
      }
      store.audit(ctx.user.id, `document.moderation.${state ?? 'cleared'}`, 'document', doc.id, { name: doc.name, note: text });
    });
    if ((state ?? null) === (doc.moderation ?? null)) {
      return;
    }
    const owners = doc.owner_kind === 'user' ? [doc.owner_id]
      : [store.get('SELECT owner_id FROM groups WHERE id = ?', doc.owner_id)?.owner_id].filter(Boolean);
    const said = text ? ` (${text})` : '';
    const message = {
      flagged: `The moderators flagged “${doc.name}”: links and public view no longer reach it${said}`,
      removed: `The moderators removed “${doc.name}”${said}`,
    }[state] ?? `The moderators restored “${doc.name}”`;
    notify(app, owners, { kind: 'moderation', text: message, link: `/d/${doc.id}`, actorId: null });
  }
}
