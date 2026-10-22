// Signing in (password or provider) and out, registering, confirming an
// email address, the signed-in person's account, and invite pages.

import {
  claimInvite, createUser, findAccount, findInvite, handleTaken, LoginRefused, newAccountStatus, resolveLogin, safeReturnPath, selfUser,
  sendConfirmation, STATUS_MESSAGES, takeEmailToken, validEmail, validHandle,
} from '../accounts.mjs';
import { editorAvailable } from '../config.mjs';
import { badRequest, HttpError, notFound, redirect } from '../http.mjs';
import { clientAddress } from '../limits.mjs';
import { finishLogin, startLogin, takeLoginState } from '../oauth.mjs';
import { hashPassword, passwordProblem, verifyPassword } from '../passwords.mjs';

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

/** Marks a handler that may be called by someone not signed in. */
function anonymous(handler) {
  handler.anonymous = true;
  return handler;
}

export function registerAuth(router, app) {
  const { config, store, sessions, mailer, attempts } = app;

  router.get('/api/me', (ctx) => {
    const settings = ctx.settings;
    return {
      user: ctx.user ? selfUser(ctx.user) : null,
      csrf: ctx.session?.csrf ?? null,
      groups: ctx.user ? myGroups(store, ctx.user.id) : [],
      settings: { beta: settings.beta, newAccounts: settings.newAccounts, banner: settings.banner },
      providers: Object.values(config.providers).map((p) => ({ id: p.id, name: p.name })),
      editor: editorAvailable(config),
      converter: app.converter.available,
      playUrl: config.playUrl,
    };
  });

  // ── Passwords ───────────────────────────────────────────────────────────

  router.post('/api/auth/login', anonymous(async (ctx) => {
    const body = await ctx.json();
    const login = String(body.login ?? '').trim().replace(/^@/, '').toLowerCase();
    const address = clientAddress(ctx.req, config.trustProxy);
    if (attempts.blocked(`login:${login}`) || attempts.blocked(`ip:${address}`)) {
      throw new HttpError(429, 'slow_down', 'Too many tries. Wait a few minutes and try again.');
    }
    const user = findAccount(store, login);
    if (!(await verifyPassword(String(body.password ?? ''), user?.password_hash))) {
      attempts.fail(`login:${login}`);
      attempts.fail(`ip:${address}`);
      throw new HttpError(401, 'bad_login', 'That name or password is not right.');
    }
    attempts.clear(`login:${login}`);
    if (user.status !== 'active') {
      throw new HttpError(403, user.status, STATUS_MESSAGES[user.status] ?? 'This account cannot sign in.');
    }
    if (ctx.session) {
      sessions.destroy(ctx.res, ctx.session);
    }
    sessions.create(ctx.res, user, ctx.req);
    return { user: selfUser(user) };
  }));

  // A new account: ready, waiting for its email to be confirmed, or waiting
  // for an admin, as the newAccounts setting says. An invite lets it in.
  router.post('/api/auth/register', anonymous(async (ctx) => {
    const body = await ctx.json();
    const address = clientAddress(ctx.req, config.trustProxy);
    if (attempts.blocked(`register:${address}`)) {
      throw new HttpError(429, 'slow_down', 'Too many new accounts from here. Try again later.');
    }
    const handle = String(body.handle ?? '').trim();
    const email = String(body.email ?? '').trim();
    const name = String(body.name ?? '').trim().slice(0, 80) || handle;
    if (!validHandle(handle)) {
      throw badRequest('A user name is 2 to 30 letters, digits, - or _, starting with a letter or digit.');
    }
    if (!validEmail(email)) {
      throw badRequest('That is not an email address.');
    }
    const problem = passwordProblem(body.password);
    if (problem) {
      throw badRequest(problem);
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
    if (handleTaken(store, handle)) {
      throw new HttpError(409, 'taken', 'That user name is taken.');
    }
    if (store.get('SELECT 1 FROM users WHERE email = ?', email)) {
      throw new HttpError(409, 'email_taken', 'There is already an account with that email address. Sign in instead.');
    }
    attempts.fail(`register:${address}`);
    const status = newAccountStatus(ctx.settings, { invited: Boolean(invite) });
    const passwordHash = await hashPassword(body.password);
    const user = store.transaction(() => {
      const made = createUser(store, { name, email, handle, status, passwordHash });
      if (invite) {
        claimInvite(store, invite, made.id);
      }
      return store.get('SELECT * FROM users WHERE id = ?', made.id);
    });
    if (status === 'pending_email') {
      sendConfirmation(store, mailer, config, user);
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

  router.patch('/api/me', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    if (body.handle !== undefined && body.handle !== user.handle) {
      if (!validHandle(body.handle)) {
        throw badRequest('A user name is 2 to 30 letters, digits, - or _, starting with a letter or digit.');
      }
      if (handleTaken(store, body.handle, user.id)) {
        throw new HttpError(409, 'taken', 'That user name is taken.');
      }
      store.run('UPDATE users SET handle = ? WHERE id = ?', body.handle, user.id);
    }
    if (typeof body.name === 'string' && body.name.trim()) {
      store.run('UPDATE users SET name = ? WHERE id = ?', body.name.trim().slice(0, 80), user.id);
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
