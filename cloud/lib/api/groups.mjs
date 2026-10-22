// Groups: members with a role (member or admin), and a shared space of
// folders and documents. The creator is the first admin; a group can have
// many, and its last admin can neither leave nor be demoted.

import { groupRole, SHARE_ROLES } from '../access.mjs';
import { findUser, makeHandle, publicUser, validHandle } from '../accounts.mjs';
import { badRequest, forbidden, HttpError, notFound } from '../http.mjs';
import { newId } from '../ids.mjs';
import { notify } from '../notify.mjs';

const MEMBER_ROLES = ['member', 'admin'];

function groupView(g) {
  return { id: g.id, handle: g.handle, name: g.name, description: g.description, defaultRole: g.default_role, joinPolicy: g.join_policy, createdAt: g.created_at };
}

function membership(ctx, needed = 'member') {
  const user = ctx.requireUser();
  const group = ctx.store.get('SELECT * FROM groups WHERE id = ?', ctx.params.id);
  const role = group ? groupRole(ctx.store, user.id, group.id) : null;
  if (!role && !(group && user.is_admin)) {
    throw notFound('No such group, or you are not in it.');
  }
  if (needed === 'admin' && role !== 'admin') {
    throw forbidden('Only the group\'s admins can do that.');
  }
  return { group, role };
}

function admins(store, groupId) {
  return store.get("SELECT COUNT(*) AS n FROM group_members WHERE group_id = ? AND role = 'admin'", groupId).n;
}

function detail(ctx, group, role) {
  const members = ctx.store.all(
    `SELECT m.role, m.added_at, u.* FROM group_members m JOIN users u ON u.id = m.user_id
     WHERE m.group_id = ? ORDER BY m.role = 'admin' DESC, u.name COLLATE NOCASE`, group.id)
    .map((m) => ({ user: publicUser(m), role: m.role, addedAt: m.added_at }));
  return { group: groupView(group), role, members };
}

function checkHandle(store, handle, exceptGroupId = null) {
  if (!validHandle(handle)) {
    throw badRequest('A handle is 2 to 30 letters, digits, - or _, starting with a letter or digit.');
  }
  if (store.get('SELECT 1 FROM users WHERE handle = ?', handle) || store.get('SELECT 1 FROM groups WHERE handle = ? AND id IS NOT ?', handle, exceptGroupId)) {
    throw new HttpError(409, 'taken', 'That handle is taken.');
  }
}

export function registerGroups(router, app) {
  router.get('/api/groups', (ctx) => {
    const user = ctx.requireUser();
    const rows = ctx.store.all(
      `SELECT g.*, m.role, (SELECT COUNT(*) FROM group_members WHERE group_id = g.id) AS members
       FROM groups g JOIN group_members m ON m.group_id = g.id WHERE m.user_id = ? ORDER BY g.name COLLATE NOCASE`, user.id);
    return { groups: rows.map((g) => ({ ...groupView(g), role: g.role, memberCount: g.members })) };
  });

  router.post('/api/groups', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    const name = String(body.name ?? '').trim().slice(0, 80);
    if (!name) {
      throw badRequest('A group needs a name.');
    }
    const handle = body.handle ? String(body.handle).trim() : makeHandle(ctx.store, name);
    checkHandle(ctx.store, handle);
    const id = newId();
    const now = Date.now();
    ctx.store.transaction(() => {
      ctx.store.run('INSERT INTO groups (id, handle, name, description, created_by, created_at) VALUES (?, ?, ?, ?, ?, ?)',
        id, handle, name, String(body.description ?? '').slice(0, 500), user.id, now);
      ctx.store.run("INSERT INTO group_members (group_id, user_id, role, added_by, added_at) VALUES (?, ?, 'admin', ?, ?)", id, user.id, user.id, now);
      ctx.store.audit(user.id, 'group.create', 'group', id, { handle });
    });
    ctx.status = 201;
    const group = ctx.store.get('SELECT * FROM groups WHERE id = ?', id);
    return detail(ctx, group, 'admin');
  });

  router.get('/api/groups/:id', (ctx) => {
    const { group, role } = membership(ctx);
    return detail(ctx, group, role);
  });

  router.patch('/api/groups/:id', async (ctx) => {
    const { group, role } = membership(ctx, 'admin');
    const body = await ctx.json();
    const { store } = ctx;
    if (typeof body.name === 'string' && body.name.trim()) {
      store.run('UPDATE groups SET name = ? WHERE id = ?', body.name.trim().slice(0, 80), group.id);
    }
    if (typeof body.description === 'string') {
      store.run('UPDATE groups SET description = ? WHERE id = ?', body.description.slice(0, 500), group.id);
    }
    if (body.handle !== undefined && body.handle !== group.handle) {
      checkHandle(store, body.handle, group.id);
      store.run('UPDATE groups SET handle = ? WHERE id = ?', body.handle, group.id);
    }
    if (body.defaultRole !== undefined) {
      if (!SHARE_ROLES.includes(body.defaultRole)) {
        throw badRequest(`The members' role is one of ${SHARE_ROLES.join(', ')}.`);
      }
      store.run('UPDATE groups SET default_role = ? WHERE id = ?', body.defaultRole, group.id);
      store.audit(ctx.user.id, 'group.default_role', 'group', group.id, { role: body.defaultRole });
    }
    return detail(ctx, store.get('SELECT * FROM groups WHERE id = ?', group.id), role);
  });

  // The group's files go to the admin who deletes it, in a folder named after it.
  router.delete('/api/groups/:id', (ctx) => {
    const { group } = membership(ctx, 'admin');
    const { store } = ctx;
    const user = ctx.user;
    store.transaction(() => {
      const hasFiles = store.get("SELECT 1 FROM documents WHERE owner_kind = 'group' AND owner_id = ?", group.id) ||
        store.get("SELECT 1 FROM folders WHERE owner_kind = 'group' AND owner_id = ?", group.id);
      if (hasFiles) {
        const folderId = newId();
        store.run("INSERT INTO folders (id, owner_kind, owner_id, parent_id, name, created_by, created_at) VALUES (?, 'user', ?, NULL, ?, ?, ?)",
          folderId, user.id, group.name, user.id, Date.now());
        store.run("UPDATE folders SET parent_id = ? WHERE owner_kind = 'group' AND owner_id = ? AND parent_id IS NULL", folderId, group.id);
        store.run("UPDATE documents SET folder_id = ? WHERE owner_kind = 'group' AND owner_id = ? AND folder_id IS NULL", folderId, group.id);
        store.run("UPDATE folders SET owner_kind = 'user', owner_id = ? WHERE owner_kind = 'group' AND owner_id = ?", user.id, group.id);
        store.run("UPDATE documents SET owner_kind = 'user', owner_id = ? WHERE owner_kind = 'group' AND owner_id = ?", user.id, group.id);
      }
      store.run("DELETE FROM shares WHERE principal_kind = 'group' AND principal_id = ?", group.id);
      store.run("DELETE FROM favorites WHERE kind = 'group' AND target_id = ?", group.id);
      store.run('DELETE FROM groups WHERE id = ?', group.id);
      store.audit(user.id, 'group.delete', 'group', group.id, { handle: group.handle });
    });
    return { ok: true };
  });

  router.post('/api/groups/:id/members', async (ctx) => {
    const { group, role } = membership(ctx, 'admin');
    const body = await ctx.json();
    const memberRole = body.role ?? 'member';
    if (!MEMBER_ROLES.includes(memberRole)) {
      throw badRequest('A member is a member or an admin.');
    }
    const user = findUser(ctx.store, body.who);
    if (!user) {
      throw notFound('No one with an account has that user name or email. They can register, or an admin can make an account for them.');
    }
    if (groupRole(ctx.store, user.id, group.id)) {
      throw new HttpError(409, 'member', 'They are in the group already.');
    }
    ctx.store.run('INSERT INTO group_members (group_id, user_id, role, added_by, added_at) VALUES (?, ?, ?, ?, ?)', group.id, user.id, memberRole, ctx.user.id, Date.now());
    ctx.store.audit(ctx.user.id, 'group.member_add', 'group', group.id, { user: user.id, role: memberRole });
    notify(app, [user.id], { kind: 'group', text: `${ctx.user.name} added you to ${group.name}`, link: `/groups/${group.id}`, actorId: ctx.user.id });
    ctx.status = 201;
    return detail(ctx, group, role);
  });

  router.patch('/api/groups/:id/members/:user', async (ctx) => {
    const { group, role } = membership(ctx, 'admin');
    const body = await ctx.json();
    if (!MEMBER_ROLES.includes(body.role)) {
      throw badRequest('A member is a member or an admin.');
    }
    const current = groupRole(ctx.store, ctx.params.user, group.id);
    if (!current) {
      throw notFound('They are not in the group.');
    }
    if (current === 'admin' && body.role !== 'admin' && admins(ctx.store, group.id) <= 1) {
      throw new HttpError(409, 'last_admin', 'A group needs an admin. Make someone else an admin first.');
    }
    ctx.store.run('UPDATE group_members SET role = ? WHERE group_id = ? AND user_id = ?', body.role, group.id, ctx.params.user);
    ctx.store.audit(ctx.user.id, 'group.member_role', 'group', group.id, { user: ctx.params.user, role: body.role });
    return detail(ctx, group, ctx.params.user === ctx.user.id ? body.role : role);
  });

  // Admins remove members; anyone may leave.
  router.delete('/api/groups/:id/members/:user', (ctx) => {
    const { group, role } = membership(ctx);
    const self = ctx.params.user === ctx.user.id;
    if (!self && role !== 'admin') {
      throw forbidden('Only the group\'s admins can remove members.');
    }
    const current = groupRole(ctx.store, ctx.params.user, group.id);
    if (!current) {
      throw notFound('They are not in the group.');
    }
    if (current === 'admin' && admins(ctx.store, group.id) <= 1) {
      throw new HttpError(409, 'last_admin', 'A group needs an admin. Make someone else an admin first, or delete the group.');
    }
    ctx.store.run('DELETE FROM group_members WHERE group_id = ? AND user_id = ?', group.id, ctx.params.user);
    ctx.store.audit(ctx.user.id, self ? 'group.leave' : 'group.member_remove', 'group', group.id, { user: ctx.params.user });
    return self ? { ok: true } : detail(ctx, group, role);
  });
}
