// Site administration: users, beta invites, settings, the audit log, totals.

import fs from 'node:fs';
import path from 'node:path';
import { createInvite, inviteView, publicUser } from '../accounts.mjs';
import { badRequest, forbidden, notFound } from '../http.mjs';
import { SETTING_DEFAULTS } from '../store.mjs';
import { usageBytes } from '../storage.mjs';

const SIGNUP_POLICIES = ['invite_only', 'open', 'closed'];

function adminUser(store, u) {
  return {
    ...publicUser(u),
    email: u.email,
    isAdmin: Boolean(u.is_admin),
    status: u.status,
    quotaMB: u.quota_mb,
    createdAt: u.created_at,
    lastSeenAt: u.last_seen_at,
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
    if (key === 'signupPolicy') {
      if (!SIGNUP_POLICIES.includes(value)) {
        throw badRequest(`Sign-up is one of ${SIGNUP_POLICIES.join(', ')}.`);
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

export function registerAdmin(router) {
  router.get('/api/admin/users', (ctx) => {
    ctx.requireAdmin();
    const q = ctx.query('q');
    const rows = q
      ? ctx.store.all("SELECT * FROM users WHERE handle LIKE ? ESCAPE '\\' OR name LIKE ? ESCAPE '\\' OR email LIKE ? ESCAPE '\\' ORDER BY created_at DESC LIMIT 200",
        ...Array(3).fill(`%${q.replace(/[\\%_]/g, (c) => `\\${c}`)}%`))
      : ctx.store.all('SELECT * FROM users ORDER BY created_at DESC LIMIT 200');
    return { users: rows.map((u) => adminUser(ctx.store, u)) };
  });

  router.patch('/api/admin/users/:id', async (ctx) => {
    const me = ctx.requireAdmin();
    const user = ctx.store.get('SELECT * FROM users WHERE id = ?', ctx.params.id);
    if (!user) {
      throw notFound('No such user.');
    }
    const body = await ctx.json();
    if (user.id === me.id && (body.isAdmin === false || body.status === 'suspended')) {
      throw forbidden('You cannot suspend yourself or remove your own admin rights.');
    }
    if (body.status !== undefined) {
      if (!['active', 'suspended'].includes(body.status)) {
        throw badRequest('Status is active or suspended.');
      }
      ctx.store.run('UPDATE users SET status = ? WHERE id = ?', body.status, user.id);
      if (body.status === 'suspended') {
        ctx.store.run('DELETE FROM sessions WHERE user_id = ?', user.id);
      }
      ctx.store.audit(me.id, `user.${body.status === 'active' ? 'unsuspend' : 'suspend'}`, 'user', user.id);
    }
    if (body.isAdmin !== undefined) {
      ctx.store.run('UPDATE users SET is_admin = ? WHERE id = ?', Boolean(body.isAdmin), user.id);
      ctx.store.audit(me.id, 'user.admin', 'user', user.id, { admin: Boolean(body.isAdmin) });
    }
    if (body.quotaMB !== undefined) {
      const quota = body.quotaMB === null ? null : Number(body.quotaMB);
      if (quota !== null && (!Number.isInteger(quota) || quota < 0)) {
        throw badRequest('The quota is a whole number of MB, or null for the default.');
      }
      ctx.store.run('UPDATE users SET quota_mb = ? WHERE id = ?', quota, user.id);
      ctx.store.audit(me.id, 'user.quota', 'user', user.id, { quotaMB: quota });
    }
    return { user: adminUser(ctx.store, ctx.store.get('SELECT * FROM users WHERE id = ?', user.id)) };
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
    if (email && !/^[^@\s]+@[^@\s]+$/.test(email)) {
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

  router.get('/api/admin/stats', (ctx) => {
    ctx.requireAdmin();
    const count = (sql) => ctx.store.get(sql).n;
    const blobs = ctx.store.get('SELECT COUNT(*) AS n, COALESCE(SUM(size), 0) AS bytes FROM blobs');
    const dbFile = path.join(ctx.config.dataDir, 'cloud.db');
    return {
      users: count('SELECT COUNT(*) AS n FROM users'),
      groups: count('SELECT COUNT(*) AS n FROM groups'),
      documents: count('SELECT COUNT(*) AS n FROM documents WHERE trashed_at IS NULL'),
      trashed: count('SELECT COUNT(*) AS n FROM documents WHERE trashed_at IS NOT NULL'),
      revisions: count('SELECT COUNT(*) AS n FROM revisions'),
      blobs: { count: blobs.n, bytes: blobs.bytes },
      databaseBytes: fs.existsSync(dbFile) ? fs.statSync(dbFile).size : 0,
    };
  });
}
