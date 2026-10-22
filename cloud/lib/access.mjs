// Who may do what. Every route asks here; nothing else decides access.
//
// A person's role on a document is the highest of:
//   - owner, when they own it, or are an admin of the group that owns it;
//   - the group's default role, when they are a member of the owning group;
//   - a share to them, or to any group they belong to;
//   - the role of a share link the request carries;
//   - viewer, when the document is public, showcase or unlisted.
// While the beta setting is on, links and visibility count only for people
// who are signed in. A trashed document is the owner's alone.

import { hashToken } from './ids.mjs';

export const ROLES = ['none', 'viewer', 'commenter', 'editor', 'owner'];
export const SHARE_ROLES = ['viewer', 'commenter', 'editor'];
export const VISIBILITIES = ['private', 'unlisted', 'public', 'showcase'];

export function rank(role) {
  return ROLES.indexOf(role);
}

export function atLeast(role, needed) {
  return rank(role) >= rank(needed);
}

function higher(a, b) {
  return rank(a) >= rank(b) ? a : b;
}

/** 'admin', 'member' or null. */
export function groupRole(store, userId, groupId) {
  if (!userId) {
    return null;
  }
  return store.get('SELECT role FROM group_members WHERE group_id = ? AND user_id = ?', groupId, userId)?.role ?? null;
}

/** A person's role over a space (their own, or a group's): what they may do to its folders. */
export function spaceRole(store, user, ownerKind, ownerId) {
  if (!user) {
    return 'none';
  }
  if (ownerKind === 'user') {
    return ownerId === user.id ? 'owner' : 'none';
  }
  const role = groupRole(store, user.id, ownerId);
  if (role === 'admin') {
    return 'owner';
  }
  if (role === 'member') {
    return store.get('SELECT default_role FROM groups WHERE id = ?', ownerId)?.default_role ?? 'viewer';
  }
  return 'none';
}

/** The share link `token` grants on `documentId`, if it is live. */
export function linkRole(store, documentId, token, now = Date.now()) {
  if (!token) {
    return 'none';
  }
  const link = store.get('SELECT role, expires_at, revoked_at FROM share_links WHERE token_hash = ? AND document_id = ?', hashToken(token), documentId);
  if (!link || link.revoked_at || (link.expires_at && link.expires_at < now)) {
    return 'none';
  }
  return link.role;
}

/**
 * @param {import('./store.mjs').Store} store
 * @param {object|null} user the signed-in user row, or null
 * @param {object} doc a documents row
 * @param {{ linkToken?: string, beta: boolean }} options
 */
export function documentRole(store, user, doc, { linkToken, beta }) {
  if (!doc) {
    return 'none';
  }
  let role = spaceRole(store, user, doc.owner_kind, doc.owner_id);
  if (doc.trashed_at) {
    return role === 'owner' ? 'owner' : 'none';
  }
  if (user) {
    const shares = store.all(
      `SELECT role FROM shares WHERE document_id = ? AND (
         (principal_kind = 'user' AND principal_id = ?) OR
         (principal_kind = 'group' AND principal_id IN (SELECT group_id FROM group_members WHERE user_id = ?)))`,
      doc.id, user.id, user.id);
    for (const share of shares) {
      role = higher(role, share.role);
    }
  }
  const outsiders = !beta || user;
  if (outsiders) {
    role = higher(role, linkRole(store, doc.id, linkToken));
    if (doc.visibility !== 'private') {
      role = higher(role, 'viewer');
    }
  }
  return role;
}

/**
 * Whether `user` may download `doc` (keep a copy of the file of their own):
 *   - its owner, and the members of the group that owns it, may;
 *   - someone it is shared with may when the share (to them, or to a group
 *     they are in) says they can download;
 *   - anyone else who can see it (by a link, or because it is public) may
 *     when the document allows downloads.
 */
export function mayDownload(store, user, doc, role, linkToken = null) {
  if (role === 'owner') {
    return true;
  }
  if (!doc || doc.trashed_at || !atLeast(role, 'viewer')) {
    return false;
  }
  if (user) {
    if (doc.owner_kind === 'group' && groupRole(store, user.id, doc.owner_id)) {
      return true;
    }
    const shared = store.get(
      `SELECT 1 FROM shares WHERE document_id = ? AND can_download = 1 AND (
         (principal_kind = 'user' AND principal_id = ?) OR
         (principal_kind = 'group' AND principal_id IN (SELECT group_id FROM group_members WHERE user_id = ?))) LIMIT 1`,
      doc.id, user.id, user.id);
    if (shared) {
      return true;
    }
  }
  return Boolean(doc.allow_download) && (doc.visibility !== 'private' || linkRole(store, doc.id, linkToken) !== 'none');
}

/**
 * Whether `role` may have the file's bytes in the web editor: editors to
 * edit it, anyone else only if they may download it.
 */
export function mayOpen(store, user, doc, role, linkToken = null) {
  return atLeast(role, 'editor') || mayDownload(store, user, doc, role, linkToken);
}
