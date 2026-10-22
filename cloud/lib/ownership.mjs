// Handing things on: a document to another person or a group, a group to
// another of its members, and what happens to both when an account goes.

import { badRequest, HttpError } from './http.mjs';
import { checkRoomForDocument } from './storage.mjs';

/**
 * Makes `to` ({ kind: 'user' | 'group', id }) the owner of `doc`. It leaves
 * its folder for the top of the new owner's files. The owner before keeps
 * working on it, as an editor who can download it, unless `keepAccess` is
 * false (their account is going).
 */
export function transferDocument(store, doc, to, byUserId, { keepAccess = true } = {}) {
  if (to.kind === doc.owner_kind && to.id === doc.owner_id) {
    throw badRequest('It is theirs already.');
  }
  checkRoomForDocument(store, doc, to.kind, to.id);
  store.transaction(() => {
    store.run('UPDATE documents SET owner_kind = ?, owner_id = ?, folder_id = NULL WHERE id = ?', to.kind, to.id, doc.id);
    // A share to the new owner says less than owning it.
    store.run('DELETE FROM shares WHERE document_id = ? AND principal_kind = ? AND principal_id = ?', doc.id, to.kind, to.id);
    if (keepAccess) {
      store.run(`INSERT INTO shares (document_id, principal_kind, principal_id, role, can_download, granted_by, granted_at)
                 VALUES (?, ?, ?, 'editor', 1, ?, ?)
                 ON CONFLICT (document_id, principal_kind, principal_id) DO UPDATE SET role = 'editor', can_download = 1`,
      doc.id, doc.owner_kind, doc.owner_id, byUserId, Date.now());
    }
    store.audit(byUserId, 'document.transfer', 'document', doc.id, { from: `${doc.owner_kind}:${doc.owner_id}`, to: `${to.kind}:${to.id}` });
  });
}

/** Makes member `toUserId` the owner of `group` (and one of its admins). */
export function transferGroup(store, group, toUserId, byUserId) {
  const member = store.get('SELECT role FROM group_members WHERE group_id = ? AND user_id = ?', group.id, toUserId);
  if (!member) {
    throw badRequest(`The new owner of ${group.name} has to be one of its members.`);
  }
  store.transaction(() => {
    store.run("UPDATE group_members SET role = 'admin' WHERE group_id = ? AND user_id = ?", group.id, toUserId);
    store.run('UPDATE groups SET owner_id = ? WHERE id = ?', toUserId, group.id);
    store.audit(byUserId, 'group.transfer', 'group', group.id, { from: group.owner_id, to: toUserId });
  });
}

/** Deletes `group` with its folders and documents. */
export function deleteGroup(store, group) {
  store.transaction(() => {
    store.run("DELETE FROM documents WHERE owner_kind = 'group' AND owner_id = ?", group.id);
    store.run("DELETE FROM folders WHERE owner_kind = 'group' AND owner_id = ?", group.id);
    store.run("DELETE FROM shares WHERE principal_kind = 'group' AND principal_id = ?", group.id);
    store.run("DELETE FROM favorites WHERE kind = 'group' AND target_id = ?", group.id);
    store.run('DELETE FROM groups WHERE id = ?', group.id);
  });
}

/**
 * Who could take over what `user` owns, for them to choose before their
 * account goes: their documents that others can open (and the people who
 * can), and their groups (and the other members).
 */
export function deletionChoices(store, user) {
  const people = (rows) => rows.map((r) => ({ id: r.id, name: r.name, handle: r.handle }));
  const documents = store.all(
    `SELECT d.* FROM documents d WHERE d.owner_kind = 'user' AND d.owner_id = ? AND d.trashed_at IS NULL
       AND d.id IN (SELECT document_id FROM shares) ORDER BY d.name COLLATE NOCASE`, user.id)
    .map((d) => ({
      id: d.id,
      name: d.name,
      // People it is shared with, and the members of groups it is shared with.
      candidates: people(store.all(
        `SELECT DISTINCT u.id, u.name, u.handle FROM users u WHERE u.status = 'active' AND u.id != ? AND (
           u.id IN (SELECT principal_id FROM shares WHERE document_id = ? AND principal_kind = 'user') OR
           u.id IN (SELECT m.user_id FROM group_members m JOIN shares s ON s.principal_kind = 'group' AND s.principal_id = m.group_id
                    WHERE s.document_id = ?))
         ORDER BY u.name COLLATE NOCASE`, user.id, d.id, d.id)),
    }));
  const groups = store.all('SELECT * FROM groups WHERE owner_id = ? ORDER BY name COLLATE NOCASE', user.id).map((g) => ({
    id: g.id,
    name: g.name,
    candidates: people(store.all(
      `SELECT u.id, u.name, u.handle FROM group_members m JOIN users u ON u.id = m.user_id
       WHERE m.group_id = ? AND u.id != ? AND u.status = 'active' ORDER BY m.role = 'admin' DESC, u.name COLLATE NOCASE`, g.id, user.id)),
  }));
  const counts = store.get(
    "SELECT COUNT(*) AS n, COALESCE(SUM(size), 0) AS bytes FROM documents WHERE owner_kind = 'user' AND owner_id = ?", user.id);
  return { documents, groups, documentCount: counts.n, bytes: counts.bytes };
}

/**
 * Deletes `user` and the documents and folders they own. Documents in
 * `documentTransfers` (id → user id) go to those people first. A group they
 * own goes to the member `groupTransfers` names (id → user id) or, with
 * `auto`, to its first admin or member; a group with no one else in it is
 * deleted with its files. Without `auto`, a group with other members and no
 * one named to take it over stops the whole deletion.
 */
export function deleteAccount(store, user, { documentTransfers = new Map(), groupTransfers = new Map(), auto = false } = {}) {
  store.transaction(() => {
    for (const group of store.all('SELECT * FROM groups WHERE owner_id = ?', user.id)) {
      const others = store.all(
        "SELECT user_id FROM group_members WHERE group_id = ? AND user_id != ? ORDER BY role = 'admin' DESC, added_at", group.id, user.id);
      const to = groupTransfers.get(group.id) ?? (auto ? others[0]?.user_id : null);
      if (to) {
        transferGroup(store, group, to, user.id);
      } else if (others.length) {
        throw new HttpError(409, 'group_transfer', `Choose who takes over ${group.name}: it has other members.`);
      } else {
        deleteGroup(store, group);
      }
    }
    for (const [documentId, toUserId] of documentTransfers) {
      const doc = store.get("SELECT * FROM documents WHERE id = ? AND owner_kind = 'user' AND owner_id = ?", documentId, user.id);
      const to = store.get("SELECT id FROM users WHERE id = ? AND status = 'active'", toUserId);
      if (!doc || !to || to.id === user.id) {
        throw badRequest('One of the documents to hand on is not yours, or the person to take it has no account.');
      }
      transferDocument(store, doc, { kind: 'user', id: to.id }, user.id, { keepAccess: false });
    }
    store.run("DELETE FROM documents WHERE owner_kind = 'user' AND owner_id = ?", user.id);
    store.run("DELETE FROM folders WHERE owner_kind = 'user' AND owner_id = ?", user.id);
    store.run("DELETE FROM shares WHERE principal_kind = 'user' AND principal_id = ?", user.id);
    store.run('DELETE FROM users WHERE id = ?', user.id);
  });
}
