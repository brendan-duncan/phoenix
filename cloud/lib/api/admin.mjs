// Site administration: accounts (making, unlocking, suspending, deleting,
// passwords, admin rights, storage), invites, settings, the audit log,
// totals and the email outbox.

import fs from 'node:fs';
import path from 'node:path';
import { createInvite, createUser, handleTaken, inviteView, publicUser, validEmail, validHandle } from '../accounts.mjs';
import { badRequest, forbidden, HttpError, notFound } from '../http.mjs';
import { hashPassword, passwordProblem } from '../passwords.mjs';
import { SETTING_DEFAULTS } from '../store.mjs';
import { usageBytes } from '../storage.mjs';

const NEW_ACCOUNTS = ['open', 'email', 'admin'];
const STATUSES = ['active', 'suspended'];

function adminUser(store, u) {
  return {
    ...publicUser(u),
    email: u.email,
    emailVerified: Boolean(u.email_verified),
    isAdmin: Boolean(u.is_admin),
    status: u.status,
    quotaMB: u.quota_mb,
    createdAt: u.created_at,
    lastSeenAt: u.last_seen_at,
    hasPassword: Boolean(u.password_hash),
    documents: store.get("SELECT COUNT(*) AS n FROM documents WHERE owner_kind = 'user' AND owner_id = ?", u.id).n,
    bytes: usageBytes(store, 'user', u.id),
    providers: store.all('SELECT provider FROM identities WHERE user_id = ?', u.id).map((i) => i.provider),
  };
}

function validateSettings(input) {
  const out = {};
  for (const [key, value] of Object.entries(input)) {
    if (!(key in SETTING_DEFAULTS)) {
      throw badRequest(`Unknown setting '${key}'.`);
    }
    if (key === 'newAccounts') {
      if (!NEW_ACCOUNTS.includes(value)) {
        throw badRequest(`New accounts are one of ${NEW_ACCOUNTS.join(', ')}.`);
      }
      out[key] = value;
    } else if (key === 'beta') {
      out[key] = Boolean(value);
    } else if (key === 'defaultQuotaMB') {
      const n = Number(value);
      if (!Number.isInteger(n) || n < 1 || n > 1000000) {
        throw badRequest('The quota is a whole number of MB from 1 to 1000000.');
      }
      out[key] = n;
    } else if (key === 'banner') {
      out[key] = String(value ?? '').slice(0, 300);
    }
  }
  return out;
}

/**
 * Deletes an account and everything it owns. Groups it is the only member
 * of go with it; a group it is the last admin of, with other members, has
 * to get another admin first.
 */
function deleteAccount(store, user) {
  store.transaction(() => {
    for (const g of store.all("SELECT g.* FROM groups g JOIN group_members m ON m.group_id = g.id WHERE m.user_id = ? AND m.role = 'admin'", user.id)) {
      const members = store.get('SELECT COUNT(*) AS n FROM group_members WHERE group_id = ?', g.id).n;
      const admins = store.get("SELECT COUNT(*) AS n FROM group_members WHERE group_id = ? AND role = 'admin'", g.id).n;
      if (members === 1) {
        store.run("DELETE FROM documents WHERE owner_kind = 'group' AND owner_id = ?", g.id);
        store.run("DELETE FROM folders WHERE owner_kind = 'group' AND owner_id = ?", g.id);
        store.run("DELETE FROM shares WHERE principal_kind = 'group' AND principal_id = ?", g.id);
        store.run('DELETE FROM groups WHERE id = ?', g.id);
      } else if (admins === 1) {
        throw new HttpError(409, 'last_admin', `They are the only admin of ${g.name}. Make someone else an admin of it first.`);
      }
    }
    store.run("DELETE FROM documents WHERE owner_kind = 'user' AND owner_id = ?", user.id);
    store.run("DELETE FROM folders WHERE owner_kind = 'user' AND owner_id = ?", user.id);
    store.run("DELETE FROM shares WHERE principal_kind = 'user' AND principal_id = ?", user.id);
    store.run('DELETE FROM users WHERE id = ?', user.id);
  });
}

export function registerAdmin(router, app) {
  router.get('/api/admin/users', (ctx) => {
    ctx.requireAdmin();
    const q = ctx.query('q');
    const rows = q
      ? ctx.store.all("SELECT * FROM users WHERE handle LIKE ? ESCAPE '\\' OR name LIKE ? ESCAPE '\\' OR email LIKE ? ESCAPE '\\' ORDER BY created_at DESC LIMIT 500",
        ...Array(3).fill(`%${q.replace(/[\\%_]/g, (c) => `\\${c}`)}%`))
      : ctx.store.all('SELECT * FROM users ORDER BY created_at DESC LIMIT 500');
    return { users: rows.map((u) => adminUser(ctx.store, u)) };
  });

  // An account an admin makes is ready to use: { handle, name, email, password, isAdmin }.
  router.post('/api/admin/users', async (ctx) => {
    const me = ctx.requireAdmin();
    const body = await ctx.json();
    const handle = String(body.handle ?? '').trim();
    const email = body.email ? String(body.email).trim() : null;
    if (!validHandle(handle)) {
      throw badRequest('A user name is 2 to 30 letters, digits, - or _, starting with a letter or digit.');
    }
    if (handleTaken(ctx.store, handle)) {
      throw new HttpError(409, 'taken', 'That user name is taken.');
    }
    if (email && !validEmail(email)) {
      throw badRequest('That is not an email address.');
    }
    if (email && ctx.store.get('SELECT 1 FROM users WHERE email = ?', email)) {
      throw new HttpError(409, 'email_taken', 'Another account has that email address.');
    }
    const problem = passwordProblem(body.password);
    if (problem) {
      throw badRequest(problem);
    }
    const user = createUser(ctx.store, {
      name: String(body.name ?? '').trim() || handle, handle, email, isAdmin: Boolean(body.isAdmin), passwordHash: await hashPassword(body.password),
    });
    ctx.store.audit(me.id, 'user.create_by_admin', 'user', user.id, { handle });
    ctx.status = 201;
    return { user: adminUser(ctx.store, user) };
  });

  // { status: 'active' | 'suspended' } unlocks, approves, suspends; also isAdmin, quotaMB, password, name, email.
  router.patch('/api/admin/users/:id', async (ctx) => {
    const me = ctx.requireAdmin();
    const user = ctx.store.get('SELECT * FROM users WHERE id = ?', ctx.params.id);
    if (!user) {
      throw notFound('No such account.');
    }
    const body = await ctx.json();
    const { store } = ctx;
    if (user.id === me.id && (body.isAdmin === false || (body.status && body.status !== 'active'))) {
      throw forbidden('You cannot suspend yourself or take away your own admin rights.');
    }
    if (body.status !== undefined) {
      if (!STATUSES.includes(body.status)) {
        throw badRequest('Status is active or suspended.');
      }
      store.run('UPDATE users SET status = ? WHERE id = ?', body.status, user.id);
      if (body.status === 'suspended') {
        store.run('DELETE FROM sessions WHERE user_id = ?', user.id);
      }
      const verb = body.status === 'suspended' ? 'suspend' : (user.status.startsWith('pending') ? 'unlock' : 'unsuspend');
      store.audit(me.id, `user.${verb}`, 'user', user.id);
    }
    if (body.isAdmin !== undefined) {
      store.run('UPDATE users SET is_admin = ? WHERE id = ?', Boolean(body.isAdmin), user.id);
      store.audit(me.id, 'user.admin', 'user', user.id, { admin: Boolean(body.isAdmin) });
    }
    if (body.quotaMB !== undefined) {
      const quota = body.quotaMB === null ? null : Number(body.quotaMB);
      if (quota !== null && (!Number.isInteger(quota) || quota < 0)) {
        throw badRequest('The quota is a whole number of MB, or empty for the default.');
      }
      store.run('UPDATE users SET quota_mb = ? WHERE id = ?', quota, user.id);
      store.audit(me.id, 'user.quota', 'user', user.id, { quotaMB: quota });
    }
    if (body.password !== undefined) {
      const problem = passwordProblem(body.password);
      if (problem) {
        throw badRequest(problem);
      }
      store.run('UPDATE users SET password_hash = ?, password_is_default = 0 WHERE id = ?', await hashPassword(body.password), user.id);
      if (user.id !== me.id) {
        store.run('DELETE FROM sessions WHERE user_id = ?', user.id);
      }
      store.audit(me.id, 'user.password_reset', 'user', user.id);
    }
    if (typeof body.name === 'string' && body.name.trim()) {
      store.run('UPDATE users SET name = ? WHERE id = ?', body.name.trim().slice(0, 80), user.id);
    }
    if (body.email !== undefined && body.email !== user.email) {
      if (body.email && !validEmail(body.email)) {
        throw badRequest('That is not an email address.');
      }
      if (body.email && store.get('SELECT 1 FROM users WHERE email = ? AND id != ?', body.email, user.id)) {
        throw new HttpError(409, 'email_taken', 'Another account has that email address.');
      }
      store.run('UPDATE users SET email = ? WHERE id = ?', body.email || null, user.id);
    }
    return { user: adminUser(store, store.get('SELECT * FROM users WHERE id = ?', user.id)) };
  });

  router.delete('/api/admin/users/:id', (ctx) => {
    const me = ctx.requireAdmin();
    const user = ctx.store.get('SELECT * FROM users WHERE id = ?', ctx.params.id);
    if (!user) {
      throw notFound('No such account.');
    }
    if (user.id === me.id) {
      throw forbidden('You cannot delete your own account here.');
    }
    deleteAccount(ctx.store, user);
    ctx.store.audit(me.id, 'user.delete', 'user', user.id, { handle: user.handle });
    return { ok: true };
  });

  router.get('/api/admin/invites', (ctx) => {
    ctx.requireAdmin();
    const rows = ctx.store.all('SELECT * FROM invites ORDER BY created_at DESC LIMIT 500');
    return {
      invites: rows.map((i) => ({
        ...inviteView(i),
        createdBy: publicUser(ctx.store.get('SELECT * FROM users WHERE id = ?', i.created_by)),
        claimedBy: publicUser(ctx.store.get('SELECT * FROM users WHERE id = ?', i.claimed_by)),
      })),
    };
  });

  router.post('/api/admin/invites', async (ctx) => {
    const me = ctx.requireAdmin();
    const body = await ctx.json();
    const email = body.email ? String(body.email).trim() : null;
    if (email && !validEmail(email)) {
      throw badRequest('That is not an email address.');
    }
    const days = body.days === undefined ? 14 : Number(body.days);
    if (!Number.isInteger(days) || days < 0 || days > 365) {
      throw badRequest('An invite lasts 1 to 365 days (0 for no limit).');
    }
    const { id, token } = createInvite(ctx.store, { email, note: body.note ?? '', makeAdmin: Boolean(body.makeAdmin), createdBy: me.id, days });
    ctx.status = 201;
    // The link is shown once; only its hash is kept.
    return { invite: inviteView(ctx.store.get('SELECT * FROM invites WHERE id = ?', id)), url: `${ctx.config.publicUrl}/invite/${token}` };
  });

  router.delete('/api/admin/invites/:id', (ctx) => {
    const me = ctx.requireAdmin();
    const { changes } = ctx.store.run('UPDATE invites SET revoked_at = ? WHERE id = ? AND claimed_at IS NULL AND revoked_at IS NULL', Date.now(), ctx.params.id);
    if (!changes) {
      throw notFound('No such open invite.');
    }
    ctx.store.audit(me.id, 'invite.revoke', 'invite', ctx.params.id);
    return { ok: true };
  });

  router.get('/api/admin/settings', (ctx) => {
    ctx.requireAdmin();
    return { settings: ctx.store.settings };
  });

  router.patch('/api/admin/settings', async (ctx) => {
    const me = ctx.requireAdmin();
    const values = validateSettings(await ctx.json());
    ctx.store.saveSettings(values);
    ctx.store.audit(me.id, 'settings.change', 'settings', '', values);
    return { settings: ctx.store.settings };
  });

  router.get('/api/admin/audit', (ctx) => {
    ctx.requireAdmin();
    const before = Number(ctx.query('before')) || Number.MAX_SAFE_INTEGER;
    const rows = ctx.store.all('SELECT * FROM audit_log WHERE id < ? ORDER BY id DESC LIMIT 200', before);
    const users = new Map();
    const user = (id) => {
      if (!users.has(id)) {
        users.set(id, publicUser(ctx.store.get('SELECT * FROM users WHERE id = ?', id)));
      }
      return users.get(id);
    };
    return {
      entries: rows.map((r) => ({ id: r.id, ts: r.ts, actor: user(r.actor_id), action: r.action, targetKind: r.target_kind, targetId: r.target_id, detail: JSON.parse(r.detail) })),
    };
  });

  // What the server would have emailed (there is no mail server yet).
  router.get('/api/admin/outbox', (ctx) => {
    ctx.requireAdmin();
    return { messages: app.mailer.recent() };
  });

  router.get('/api/admin/stats', (ctx) => {
    ctx.requireAdmin();
    const count = (sql) => ctx.store.get(sql).n;
    const blobs = ctx.store.get('SELECT COUNT(*) AS n, COALESCE(SUM(size), 0) AS bytes FROM blobs');
    const dbFile = path.join(ctx.config.dataDir, 'cloud.db');
    return {
      users: count('SELECT COUNT(*) AS n FROM users'),
      waiting: count("SELECT COUNT(*) AS n FROM users WHERE status LIKE 'pending%'"),
      groups: count('SELECT COUNT(*) AS n FROM groups'),
      documents: count('SELECT COUNT(*) AS n FROM documents WHERE trashed_at IS NULL'),
      trashed: count('SELECT COUNT(*) AS n FROM documents WHERE trashed_at IS NOT NULL'),
      revisions: count('SELECT COUNT(*) AS n FROM revisions'),
      blobs: { count: blobs.n, bytes: blobs.bytes },
      databaseBytes: fs.existsSync(dbFile) ? fs.statSync(dbFile).size : 0,
      converter: app.converter.available,
    };
  });
}
