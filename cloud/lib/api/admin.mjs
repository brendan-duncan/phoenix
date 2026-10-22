// Site administration: accounts (making, unlocking, suspending, deleting,
// passwords, admin rights, storage), invites, settings, the audit log,
// totals and the email outbox.

import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { createInvite, createUser, inviteView, normalizeName, publicUser, renameUser, requireName, validEmail } from '../accounts.mjs';
import { addFont, fontName, listFonts } from '../fonts.mjs';
import { badRequest, forbidden, HttpError, notFound } from '../http.mjs';
import { notify } from '../notify.mjs';
import { hashPassword, passwordProblem } from '../passwords.mjs';
import { deleteAccount } from '../ownership.mjs';
import { effectivePlan, PLANS, validPlan } from '../plans.mjs';
import { SETTING_DEFAULTS } from '../store.mjs';
import { usageBytes } from '../storage.mjs';

const NEW_ACCOUNTS = ['open', 'email', 'admin'];

function offeredPlans() {
  return Object.keys(PLANS).filter((p) => PLANS[p].offered);
}
const STATUSES = ['active', 'suspended'];

function adminUser(store, u) {
  return {
    ...publicUser(u),
    email: u.email,
    emailVerified: Boolean(u.email_verified),
    isAdmin: Boolean(u.is_admin),
    plan: u.plan,
    effectivePlan: effectivePlan(store, u),
    status: u.status,
    quotaMB: u.quota_mb,
    createdAt: u.created_at,
    lastSeenAt: u.last_seen_at,
    hasPassword: Boolean(u.password_hash),
    requestMessage: u.request_message,
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
    } else if (key === 'defaultPlan') {
      if (!validPlan(value) || !PLANS[value].offered) {
        throw badRequest(`New accounts start on one of the plans offered: ${offeredPlans().join(', ')}.`);
      }
      out[key] = value;
    } else if (key === 'beta' || key === 'registration' || key === 'locked') {
      out[key] = Boolean(value);
    } else if (key === 'lockMessage') {
      out[key] = String(value ?? '').slice(0, 500);
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

  // One account, for its page under Admin: the account, its groups, what it owns.
  router.get('/api/admin/users/:id', (ctx) => {
    ctx.requireAdmin();
    const user = ctx.store.get('SELECT * FROM users WHERE id = ?', ctx.params.id);
    if (!user) {
      throw notFound('No such account.');
    }
    const { store } = ctx;
    return {
      user: { ...adminUser(store, user), bio: user.bio, emailVerified: Boolean(user.email_verified), hasAvatar: Boolean(user.avatar_blob || user.avatar_url) },
      groups: store.all(
        `SELECT g.id, g.name, g.handle, m.role, g.owner_id = ? AS owner FROM group_members m JOIN groups g ON g.id = m.group_id
         WHERE m.user_id = ? ORDER BY g.name COLLATE NOCASE`, user.id, user.id)
        .map((g) => ({ id: g.id, name: g.name, handle: g.handle, role: g.role, owner: Boolean(g.owner) })),
      counts: {
        trashed: store.get("SELECT COUNT(*) AS n FROM documents WHERE owner_kind = 'user' AND owner_id = ? AND trashed_at IS NOT NULL", user.id).n,
        public: store.get("SELECT COUNT(*) AS n FROM documents WHERE owner_kind = 'user' AND owner_id = ? AND visibility != 'private'", user.id).n,
        moderated: store.get("SELECT COUNT(*) AS n FROM documents WHERE owner_kind = 'user' AND owner_id = ? AND moderation IS NOT NULL", user.id).n,
        messages: store.get('SELECT COUNT(*) AS n FROM messages WHERE sender_id = ?', user.id).n,
        reportsAgainst: store.get('SELECT COUNT(*) AS n FROM message_reports r JOIN messages m ON m.id = r.message_id WHERE m.sender_id = ?', user.id).n
          + store.get("SELECT COUNT(*) AS n FROM document_reports r JOIN documents d ON d.id = r.document_id WHERE d.owner_kind = 'user' AND d.owner_id = ?", user.id).n,
      },
    };
  });

  // An account an admin makes is ready to use: { name, email, password, isAdmin }.
  router.post('/api/admin/users', async (ctx) => {
    const me = ctx.requireAdmin();
    const body = await ctx.json();
    const name = requireName(ctx.store, body.name);
    const email = body.email ? String(body.email).trim() : null;
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
      name, email, isAdmin: Boolean(body.isAdmin), passwordHash: await hashPassword(body.password),
    });
    ctx.store.audit(me.id, 'user.create_by_admin', 'user', user.id, { name });
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
      if (verb === 'unlock') {
        notify(app, [user.id], {
          kind: 'unlocked', text: 'Your account is ready. Welcome to Phoenix Cloud!', link: '/', actorId: me.id,
          email: { subject: 'Your Phoenix Cloud account is ready', text: 'An admin has unlocked your account. Sign in here:' },
        });
      }
    }
    // An offensive profile: its picture or its words cleared.
    if (body.clearAvatar) {
      store.run("UPDATE users SET avatar_blob = NULL, avatar_url = '' WHERE id = ?", user.id);
      store.audit(me.id, 'user.clear_avatar', 'user', user.id);
    }
    if (body.clearBio) {
      store.run("UPDATE users SET bio = '' WHERE id = ?", user.id);
      store.audit(me.id, 'user.clear_bio', 'user', user.id);
    }
    if (body.plan !== undefined) {
      if (!validPlan(body.plan) || !PLANS[body.plan].offered) {
        throw badRequest(`A plan is one of ${offeredPlans().join(', ')}.`);
      }
      store.run('UPDATE users SET plan = ? WHERE id = ?', body.plan, user.id);
      store.audit(me.id, 'user.plan', 'user', user.id, { plan: body.plan });
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
    if (body.name !== undefined && normalizeName(body.name) !== user.name) {
      renameUser(store, user.id, requireName(store, body.name, user.id));
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
    // Groups they own go to another of their members.
    deleteAccount(ctx.store, user, { auto: true });
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

  router.get('/api/admin/plans', (ctx) => {
    ctx.requireAdmin();
    return { plans: Object.entries(PLANS).map(([id, p]) => ({ id, label: p.label, offered: p.offered })) };
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

  router.get('/api/admin/fonts', (ctx) => {
    ctx.requireAdmin();
    return { fonts: listFonts(ctx.config.fontsDir) };
  });

  // Adds a font (?name=File.ttf); the body is the font file.
  router.post('/api/admin/fonts', async (ctx) => {
    const me = ctx.requireAdmin();
    const { file } = await ctx.upload('font', 20 * 1024 * 1024);
    const name = await addFont(ctx.config.fontsDir, ctx.query('name'), file);
    if (!name) {
      throw badRequest('A font is a .ttf or .otf file, up to 20 MB.');
    }
    ctx.store.audit(me.id, 'font.add', 'font', name);
    ctx.status = 201;
    return { fonts: listFonts(ctx.config.fontsDir) };
  });

  router.delete('/api/admin/fonts/:name', async (ctx) => {
    const me = ctx.requireAdmin();
    const name = fontName(ctx.params.name);
    if (!name || name !== ctx.params.name) {
      throw notFound('No such font.');
    }
    await fsp.rm(path.join(ctx.config.fontsDir, name), { force: true });
    ctx.store.audit(me.id, 'font.remove', 'font', name);
    return { fonts: listFonts(ctx.config.fontsDir) };
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
