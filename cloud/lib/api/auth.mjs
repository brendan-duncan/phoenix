// Signing in (password or provider) and out, registering, confirming an
// email address, the signed-in person's account, and invite pages.

import fsp from 'node:fs/promises';
import {
  claimInvite, createUser, findAccount, findInvite, LoginRefused, makeEmailToken, nameProblem, newAccountStatus, normalizeName, renameUser, requireName,
  resolveLogin, safeReturnPath, selfUser, sendConfirmation, STATUS_MESSAGES, takeEmailToken, validEmail,
} from '../accounts.mjs';
import { editorAvailable } from '../config.mjs';
import { fontName, listFonts } from '../fonts.mjs';
import { badRequest, HttpError, lockedError, notFound, redirect, serveStatic } from '../http.mjs';
import { clientAddress } from '../limits.mjs';
import { adminIds, notificationsView, notify, unreadCount } from '../notify.mjs';
import { finishLogin, startLogin, takeLoginState } from '../oauth.mjs';
import { deleteAccount, deletionChoices } from '../ownership.mjs';
import { effectivePlan, managingTeam, PLANS } from '../plans.mjs';
import { hashPassword, passwordProblem, verifyPassword } from '../passwords.mjs';
import { keepPicture } from '../storage.mjs';

/** A picture's media type from its first bytes, or null when it is not one we take. */
function pictureType(bytes) {
  if (bytes.length >= 8 && bytes.readUInt32BE(0) === 0x89504e47) {
    return 'image/png';
  }
  if (bytes.length >= 3 && bytes[0] === 0xff && bytes[1] === 0xd8 && bytes[2] === 0xff) {
    return 'image/jpeg';
  }
  if (bytes.length >= 12 && bytes.toString('latin1', 0, 4) === 'RIFF' && bytes.toString('latin1', 8, 12) === 'WEBP') {
    return 'image/webp';
  }
  if (bytes.length >= 6 && bytes.toString('latin1', 0, 4) === 'GIF8') {
    return 'image/gif';
  }
  return null;
}

function myGroups(store, userId) {
  return store.all(
    `SELECT g.id, g.handle, g.name, m.role FROM groups g JOIN group_members m ON m.group_id = g.id
     WHERE m.user_id = ? ORDER BY g.name COLLATE NOCASE`, userId)
    .map((g) => ({ id: g.id, handle: g.handle, name: g.name, role: g.role }));
}

/** "b•••@gmail.com": enough to recognize an invite's address, not to harvest it. */
function maskEmail(email) {
  const [local, domain] = String(email).split('@');
  return `${local.slice(0, 1)}•••@${domain ?? ''}`;
}

const MAX_REQUEST_MESSAGE = 1000;
const REGISTRATION_CLOSED = 'New accounts are closed for now. Someone with an account can ask an admin for an invite for you.';

/** Marks a handler that may be called by someone not signed in. */
function anonymous(handler) {
  handler.anonymous = true;
  return handler;
}

function planView({ plan, team, managed }) {
  return { id: plan, label: PLANS[plan]?.label ?? plan, team, managed };
}

export function registerAuth(router, app) {
  const { config, store, sessions, mailer, attempts } = app;
  const lastAdmin = (user) => Boolean(user.is_admin) &&
    store.get("SELECT COUNT(*) AS n FROM users WHERE is_admin = 1 AND status = 'active'").n <= 1;

  router.get('/api/me', (ctx) => {
    const settings = ctx.settings;
    return {
      user: ctx.user ? selfUser(ctx.user) : null,
      csrf: ctx.session?.csrf ?? null,
      groups: ctx.user ? myGroups(store, ctx.user.id) : [],
      settings: {
        beta: settings.beta,
        newAccounts: settings.newAccounts,
        registration: settings.registration,
        banner: settings.banner,
        locked: settings.locked,
        lockMessage: settings.lockMessage,
      },
      providers: Object.values(config.providers).map((p) => ({ id: p.id, name: p.name })),
      editor: editorAvailable(config),
      unread: ctx.user ? unreadCount(store, ctx.user.id) : 0,
      plan: ctx.user ? planView(effectivePlan(store, ctx.user)) : null,
      converter: app.converter.available,
      playUrl: config.playUrl,
    };
  });

  // ── Passwords ───────────────────────────────────────────────────────────

  router.post('/api/auth/login', anonymous(async (ctx) => {
    const body = await ctx.json();
    const login = String(body.login ?? '').trim().replace(/^@/, '').toLowerCase();
    const address = clientAddress(ctx.req, config.trustProxy);
    // Failures count per name from one address, so nobody else can lock
    // an account (the admin's, say) by guessing at it.
    const tries = `login:${login}:${address}`;
    if (attempts.blocked(tries) || attempts.blocked(`ip:${address}`)) {
      throw new HttpError(429, 'slow_down', 'Too many tries. Wait a few minutes and try again.');
    }
    const user = findAccount(store, login);
    if (!(await verifyPassword(String(body.password ?? ''), user?.password_hash))) {
      attempts.fail(tries);
      attempts.fail(`ip:${address}`);
      throw new HttpError(401, 'bad_login', 'That name or password is not right.');
    }
    attempts.clear(tries);
    if (user.status !== 'active') {
      throw new HttpError(403, user.status, STATUS_MESSAGES[user.status] ?? 'This account cannot sign in.');
    }
    if (ctx.settings.locked && !user.is_admin) {
      throw lockedError(ctx.settings);
    }
    if (ctx.session) {
      sessions.destroy(ctx.res, ctx.session);
    }
    sessions.create(ctx.res, user, ctx.req);
    return { user: selfUser(user) };
  }));

  // Whether a name can be registered (or taken by the signed-in person), as it is typed.
  router.get('/api/auth/name', anonymous((ctx) => {
    const name = normalizeName(ctx.query('name'));
    const problem = nameProblem(store, name, ctx.user?.id ?? null);
    return { name, available: !problem, problem };
  }));

  // A new account: ready, waiting for its email to be confirmed, or waiting
  // for an admin, as the newAccounts setting says. An invite lets it in.
  router.post('/api/auth/register', anonymous(async (ctx) => {
    const body = await ctx.json();
    const address = clientAddress(ctx.req, config.trustProxy);
    if (attempts.blocked(`register:${address}`)) {
      throw new HttpError(429, 'slow_down', 'Too many new accounts from here. Try again later.');
    }
    const name = requireName(store, body.name);
    const email = String(body.email ?? '').trim();
    if (!validEmail(email)) {
      throw badRequest('That is not an email address.');
    }
    const problem = passwordProblem(body.password);
    if (problem) {
      throw badRequest(problem);
    }
    if (!ctx.settings.registration && !body.invite) {
      throw new HttpError(403, 'registration_closed', REGISTRATION_CLOSED);
    }
    let invite = null;
    if (body.invite) {
      invite = findInvite(store, body.invite);
      if (!invite) {
        throw new HttpError(400, 'invite_invalid', 'That invite has been used, has run out or was turned off.');
      }
      if (invite.email && invite.email.toLowerCase() !== email.toLowerCase()) {
        throw new HttpError(400, 'invite_email', `That invite is for another email address.`);
      }
    }
    if (store.get('SELECT 1 FROM users WHERE email = ?', email)) {
      throw new HttpError(409, 'email_taken', 'There is already an account with that email address. Sign in instead.');
    }
    attempts.fail(`register:${address}`);
    const status = newAccountStatus(ctx.settings, { invited: Boolean(invite) });
    // An account an admin has to unlock comes with a word about who is asking.
    const requestMessage = String(body.message ?? '').trim().slice(0, MAX_REQUEST_MESSAGE);
    if (status === 'pending_admin' && !requestMessage) {
      throw badRequest('Say a little about yourself, so the admin knows who is asking.');
    }
    const passwordHash = await hashPassword(body.password);
    const user = store.transaction(() => {
      const made = createUser(store, { name, email, status, passwordHash, requestMessage: status === 'pending_admin' ? requestMessage : '' });
      if (invite) {
        claimInvite(store, invite, made.id);
      }
      return store.get('SELECT * FROM users WHERE id = ?', made.id);
    });
    if (status === 'pending_email') {
      sendConfirmation(store, mailer, config, user);
    }
    if (status === 'pending_admin') {
      notify(app, adminIds(store), { kind: 'waiting', text: `${user.name} asked for an account`, link: '/admin', actorId: user.id });
    }
    if (status === 'active') {
      sessions.create(ctx.res, user, ctx.req);
    }
    ctx.status = 201;
    return { status, message: STATUS_MESSAGES[status] ?? null };
  }));

  // From the link in the confirmation email. Signs them in when that unlocks the account.
  router.post('/api/auth/confirm', anonymous(async (ctx) => {
    const body = await ctx.json();
    const userId = takeEmailToken(store, String(body.token ?? ''), 'confirm');
    if (!userId) {
      throw new HttpError(400, 'token', 'That link has been used or has run out. Sign in to have a new one sent.');
    }
    store.run('UPDATE users SET email_verified = 1 WHERE id = ?', userId);
    store.run("UPDATE users SET status = 'active' WHERE id = ? AND status = 'pending_email'", userId);
    const user = store.get('SELECT * FROM users WHERE id = ?', userId);
    store.audit(userId, 'user.confirm_email', 'user', userId);
    if (user.status === 'active') {
      sessions.create(ctx.res, user, ctx.req);
    }
    return { status: user.status, message: STATUS_MESSAGES[user.status] ?? null };
  }));

  // Another confirmation email, for someone whose link ran out.
  router.post('/api/auth/resend', anonymous(async (ctx) => {
    const body = await ctx.json();
    const address = clientAddress(ctx.req, config.trustProxy);
    if (attempts.blocked(`resend:${address}`)) {
      throw new HttpError(429, 'slow_down', 'Too many emails asked for. Try again later.');
    }
    attempts.fail(`resend:${address}`);
    const user = findAccount(store, body.login);
    if (user?.status === 'pending_email' && user.email) {
      sendConfirmation(store, mailer, config, user);
    }
    // The same answer either way, so this can't be used to find accounts.
    return { ok: true };
  }));

  // A link to choose a new password, by email. The same answer whether or
  // not there is such an account.
  router.post('/api/auth/forgot', anonymous(async (ctx) => {
    const body = await ctx.json();
    const address = clientAddress(ctx.req, config.trustProxy);
    if (attempts.blocked(`forgot:${address}`)) {
      throw new HttpError(429, 'slow_down', 'Too many emails asked for. Try again later.');
    }
    attempts.fail(`forgot:${address}`);
    const user = findAccount(store, body.email);
    if (user?.status === 'active' && user.email) {
      const token = makeEmailToken(store, user.id, 'reset', 2);
      mailer.send({
        to: user.email,
        subject: 'A new Phoenix Cloud password',
        text: `Hello ${user.name},\n\nChoose a new password for Phoenix Cloud here:\n${config.publicUrl}/reset/${token}\n\nThe link works for 2 hours. If you did not ask for it, ignore this email: your password stays as it is.`,
      });
    }
    return { ok: true };
  }));

  // From that link: sets the password and signs them in (and out everywhere else).
  router.post('/api/auth/reset', anonymous(async (ctx) => {
    const body = await ctx.json();
    const problem = passwordProblem(body.password);
    if (problem) {
      throw badRequest(problem);
    }
    const userId = takeEmailToken(store, String(body.token ?? ''), 'reset');
    const user = userId ? store.get('SELECT * FROM users WHERE id = ?', userId) : null;
    if (!user || user.status !== 'active') {
      throw new HttpError(400, 'token', 'That link has been used or has run out. Ask for a new one.');
    }
    store.run('UPDATE users SET password_hash = ?, password_is_default = 0 WHERE id = ?', await hashPassword(body.password), user.id);
    store.run('DELETE FROM sessions WHERE user_id = ?', user.id);
    store.audit(user.id, 'user.password_reset_by_email', 'user', user.id);
    sessions.create(ctx.res, user, ctx.req);
    return { ok: true };
  }));

  // A profile picture: PNG, JPEG, WebP or GIF, up to 2 MB.
  router.put('/api/me/avatar', async (ctx) => {
    const user = ctx.requireUser();
    const { file } = await ctx.upload('img', 2 * 1024 * 1024);
    const bytes = await fsp.readFile(file);
    if (!pictureType(bytes)) {
      throw badRequest('A picture is a PNG, JPEG, WebP or GIF.');
    }
    const hash = await keepPicture(store, ctx.blobs, bytes);
    store.run('UPDATE users SET avatar_blob = ?, avatar_url = ? WHERE id = ?', hash, `/api/users/${user.id}/avatar?v=${hash.slice(0, 12)}`, user.id);
    return { user: selfUser(store.get('SELECT * FROM users WHERE id = ?', user.id)) };
  });

  router.delete('/api/me/avatar', (ctx) => {
    const user = ctx.requireUser();
    store.run("UPDATE users SET avatar_blob = NULL, avatar_url = '' WHERE id = ?", user.id);
    return { user: selfUser(store.get('SELECT * FROM users WHERE id = ?', user.id)) };
  });

  router.get('/api/users/:id/avatar', async (ctx) => {
    ctx.requireUser();
    const row = store.get('SELECT avatar_blob FROM users WHERE id = ?', ctx.params.id);
    if (!row?.avatar_blob) {
      throw notFound('No picture.');
    }
    const bytes = await ctx.blobs.get(row.avatar_blob);
    ctx.res.writeHead(200, { 'Content-Type': pictureType(bytes), 'Content-Length': bytes.length, 'Cache-Control': 'private, max-age=31536000, immutable' });
    ctx.res.end(bytes);
    return undefined;
  });

  // ── Notifications ───────────────────────────────────────────────────────

  router.get('/api/notifications', (ctx) => {
    const user = ctx.requireUser();
    return { notifications: notificationsView(store, user.id), unread: unreadCount(store, user.id) };
  });

  // Marks them read: { ids } or, with none, all.
  router.post('/api/notifications/read', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    const now = Date.now();
    if (Array.isArray(body.ids) && body.ids.length) {
      for (const id of body.ids.slice(0, 500)) {
        store.run('UPDATE notifications SET read_at = ? WHERE id = ? AND user_id = ? AND read_at IS NULL', now, Number(id), user.id);
      }
    } else {
      store.run('UPDATE notifications SET read_at = ? WHERE user_id = ? AND read_at IS NULL', now, user.id);
    }
    return { unread: unreadCount(store, user.id) };
  });

  // ── The font library ────────────────────────────────────────────────────

  router.get('/api/fonts', (ctx) => {
    ctx.requireUser();
    return { fonts: listFonts(config.fontsDir).map((f) => ({ ...f, url: `/api/fonts/${encodeURIComponent(f.name)}` })) };
  });

  router.get('/api/fonts/:name', async (ctx) => {
    ctx.requireUser();
    const name = fontName(ctx.params.name);
    if (!name || name !== ctx.params.name || !(await serveStatic(ctx.req, ctx.res, config.fontsDir, name, {
      'Cache-Control': 'private, max-age=86400',
      'Cross-Origin-Resource-Policy': 'same-origin',
    }))) {
      throw notFound('No such font.');
    }
    return undefined;
  });

  router.patch('/api/me/password', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    if (user.password_hash && !(await verifyPassword(String(body.current ?? ''), user.password_hash))) {
      throw new HttpError(403, 'bad_login', 'Your current password is not right.');
    }
    const problem = passwordProblem(body.password);
    if (problem) {
      throw badRequest(problem);
    }
    store.run('UPDATE users SET password_hash = ?, password_is_default = 0 WHERE id = ?', await hashPassword(body.password), user.id);
    sessions.endOthers(user.id, ctx.session);
    store.audit(user.id, 'user.password', 'user', user.id);
    return { ok: true };
  });

  // What deleting the account would take with it, and who could take
  // over the documents others use and the groups they own.
  router.get('/api/me/deletion', (ctx) => {
    const user = ctx.requireUser();
    const team = managingTeam(store, user.id);
    return {
      ...deletionChoices(store, user),
      managedBy: team ? { id: team.id, name: team.name } : null,
      lastAdmin: lastAdmin(user),
      needsPassword: Boolean(user.password_hash),
    };
  });

  // Deletes the account: { password (or name, without one), documents:
  // { id: userId }, groups: { id: userId } } hand those on first.
  router.delete('/api/me', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    const team = managingTeam(store, user.id);
    if (team) {
      throw new HttpError(403, 'managed', `${team.name} manages your account. Ask its owner to remove it.`);
    }
    if (lastAdmin(user)) {
      throw new HttpError(409, 'last_admin', 'You are the only admin of Phoenix Cloud. Make someone else an admin first.');
    }
    const confirmed = user.password_hash
      ? await verifyPassword(String(body.password ?? ''), user.password_hash)
      : String(body.name ?? '').trim().toLowerCase() === user.name.toLowerCase();
    if (!confirmed) {
      throw new HttpError(403, 'confirm', user.password_hash ? 'That password is not right.' : 'Type your name to confirm.');
    }
    const entries = (o) => new Map(Object.entries(o && typeof o === 'object' ? o : {}).filter(([, to]) => typeof to === 'string' && to));
    deleteAccount(store, user, { documentTransfers: entries(body.documents), groupTransfers: entries(body.groups) });
    store.audit(null, 'user.delete_self', 'user', user.id, { handle: user.handle });
    sessions.destroy(ctx.res, ctx.session);
    return { ok: true };
  });

  router.patch('/api/me', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    if (body.name !== undefined && normalizeName(body.name) !== user.name) {
      renameUser(store, user.id, requireName(store, body.name, user.id));
    }
    if (body.email !== undefined && body.email !== user.email) {
      if (!validEmail(body.email)) {
        throw badRequest('That is not an email address.');
      }
      if (store.get('SELECT 1 FROM users WHERE email = ? AND id != ?', body.email, user.id)) {
        throw new HttpError(409, 'email_taken', 'Another account has that email address.');
      }
      store.run('UPDATE users SET email = ?, email_verified = 0 WHERE id = ?', body.email, user.id);
    }
    if (typeof body.bio === 'string') {
      store.run('UPDATE users SET bio = ? WHERE id = ?', body.bio.slice(0, 500), user.id);
    }
    return { user: selfUser(store.get('SELECT * FROM users WHERE id = ?', user.id)) };
  });

  router.get('/api/me/sessions', (ctx) => {
    const user = ctx.requireUser();
    return { sessions: sessions.list(user.id, ctx.session) };
  });

  router.delete('/api/me/sessions/:id', (ctx) => {
    const user = ctx.requireUser();
    if (!sessions.revoke(user.id, ctx.params.id)) {
      throw notFound('No such session.');
    }
    return { ok: true };
  });

  router.get('/api/me/identities', (ctx) => {
    const user = ctx.requireUser();
    return {
      identities: store.all('SELECT provider, subject, email, created_at, last_used_at FROM identities WHERE user_id = ? ORDER BY created_at', user.id)
        .map((i) => ({ provider: i.provider, subject: i.subject, email: i.email, createdAt: i.created_at, lastUsedAt: i.last_used_at })),
    };
  });

  router.delete('/api/me/identities/:provider/:subject', (ctx) => {
    const user = ctx.requireUser();
    const count = store.get('SELECT COUNT(*) AS n FROM identities WHERE user_id = ?', user.id).n;
    if (count <= 1 && !user.password_hash) {
      throw new HttpError(409, 'last_identity', 'This is the only way you sign in; set a password or link another first.');
    }
    const { changes } = store.run('DELETE FROM identities WHERE user_id = ? AND provider = ? AND subject = ?', user.id, ctx.params.provider, ctx.params.subject);
    if (!changes) {
      throw notFound('No such sign-in.');
    }
    store.audit(user.id, 'identity.unlink', 'user', user.id, { provider: ctx.params.provider });
    return { ok: true };
  });

  // ── Providers ───────────────────────────────────────────────────────────

  // Full-page redirect to the provider. ?invite= carries an invite,
  // ?link=1 adds the provider to the signed-in account.
  router.get('/api/auth/start/:provider', (ctx) => {
    const provider = config.providers[ctx.params.provider];
    if (!provider) {
      throw notFound('That sign-in is not set up.');
    }
    const linking = ctx.query('link') === '1';
    if (linking) {
      ctx.requireUser();
    }
    const url = startLogin(store, config, provider, {
      invite: ctx.query('invite'),
      linkUserId: linking ? ctx.user.id : null,
      returnTo: safeReturnPath(ctx.query('return') ?? (linking ? '/account' : '/')),
    });
    redirect(ctx.res, url);
  });

  router.get('/api/auth/callback/:provider', async (ctx) => {
    const provider = config.providers[ctx.params.provider];
    const state = provider ? takeLoginState(store, config, provider.id, ctx.query('state')) : null;
    const failTo = state?.link_user_id ? '/account' : '/signin';
    const fail = (code) => redirect(ctx.res, `${failTo}?error=${code}${state?.invite ? `&invite=${encodeURIComponent(state.invite)}` : ''}`);
    if (!state) {
      fail('state');
      return;
    }
    if (ctx.query('error') || !ctx.query('code')) {
      fail('cancelled');
      return;
    }
    let profile;
    try {
      profile = await finishLogin(config, provider, app.jwks, { code: ctx.query('code'), verifier: state.verifier, nonce: state.nonce });
    } catch (err) {
      console.error(`[cloud] ${provider.id} sign-in failed:`, err.message);
      fail('failed');
      return;
    }
    let user;
    try {
      user = resolveLogin(store, ctx.settings, provider.id, profile, { invite: state.invite, linkUserId: state.link_user_id });
    } catch (err) {
      if (err instanceof LoginRefused) {
        fail(err.code);
        return;
      }
      throw err;
    }
    if (user.pending) {
      fail(user.pending);
      return;
    }
    if (ctx.settings.locked && !user.is_admin && !state.link_user_id) {
      fail('locked');
      return;
    }
    if (!state.link_user_id) {
      sessions.create(ctx.res, user, ctx.req);
    }
    redirect(ctx.res, safeReturnPath(state.return_to));
  });

  router.post('/api/auth/logout', (ctx) => {
    sessions.destroy(ctx.res, ctx.session);
    return { ok: true };
  });

  router.get('/api/invites/:token', (ctx) => {
    const invite = findInvite(store, ctx.params.token);
    if (!invite) {
      return { valid: false };
    }
    return { valid: true, email: invite.email ? maskEmail(invite.email) : null, note: invite.note };
  });
}
