// Signing in and out, the signed-in person's account, and invite pages.

import { createUser, findInvite, findUser, LoginRefused, publicUser, resolveLogin, safeReturnPath, selfUser, validHandle } from '../accounts.mjs';
import { editorAvailable } from '../config.mjs';
import { badRequest, HttpError, notFound, redirect } from '../http.mjs';
import { finishLogin, startLogin, takeLoginState } from '../oauth.mjs';

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

export function registerAuth(router, app) {
  const { config, store, sessions } = app;

  router.get('/api/me', (ctx) => {
    const settings = ctx.settings;
    return {
      user: ctx.user ? selfUser(ctx.user) : null,
      csrf: ctx.session?.csrf ?? null,
      groups: ctx.user ? myGroups(store, ctx.user.id) : [],
      settings: { beta: settings.beta, signupPolicy: settings.signupPolicy, banner: settings.banner },
      providers: Object.values(config.providers).map((p) => ({ id: p.id, name: p.name })),
      devLogin: config.devLogin,
      editor: editorAvailable(config),
      playUrl: config.playUrl,
    };
  });

  router.patch('/api/me', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    if (body.handle !== undefined && body.handle !== user.handle) {
      if (!validHandle(body.handle)) {
        throw badRequest('A handle is 2 to 30 letters, digits, - or _, starting with a letter or digit.');
      }
      if (store.get('SELECT 1 FROM users WHERE handle = ? AND id != ?', body.handle, user.id) || store.get('SELECT 1 FROM groups WHERE handle = ?', body.handle)) {
        throw new HttpError(409, 'taken', 'That handle is taken.');
      }
      store.run('UPDATE users SET handle = ? WHERE id = ?', body.handle, user.id);
    }
    if (typeof body.name === 'string' && body.name.trim()) {
      store.run('UPDATE users SET name = ? WHERE id = ?', body.name.trim().slice(0, 80), user.id);
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
    if (count <= 1) {
      throw new HttpError(409, 'last_identity', 'This is the only way you sign in; link another before removing it.');
    }
    const { changes } = store.run('DELETE FROM identities WHERE user_id = ? AND provider = ? AND subject = ?', user.id, ctx.params.provider, ctx.params.subject);
    if (!changes) {
      throw notFound('No such sign-in.');
    }
    store.audit(user.id, 'identity.unlink', 'user', user.id, { provider: ctx.params.provider });
    return { ok: true };
  });

  // Full-page redirect to the provider. ?invite= carries a beta invite,
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
    if (!state.link_user_id) {
      sessions.create(ctx.res, user, ctx.req);
    }
    redirect(ctx.res, safeReturnPath(state.return_to));
  });

  // Dev only: sign in as any handle, creating the user. The first one is an admin.
  const devLogin = async (ctx) => {
    if (!config.devLogin) {
      throw notFound();
    }
    const body = await ctx.json();
    const handle = String(body.handle ?? '').trim().toLowerCase();
    if (!validHandle(handle)) {
      throw badRequest('A handle is 2 to 30 letters, digits, - or _.');
    }
    let user = findUser(store, handle);
    if (!user) {
      if (store.get('SELECT 1 FROM users WHERE handle = ?', handle)) {
        throw new HttpError(403, 'suspended', 'That user is suspended.');
      }
      const first = !store.get('SELECT 1 FROM users LIMIT 1');
      user = createUser(store, { name: handle.charAt(0).toUpperCase() + handle.slice(1), email: `${handle}@dev.local`, handle, isAdmin: first });
    }
    if (ctx.session) {
      sessions.destroy(ctx.res, ctx.session);
    }
    sessions.create(ctx.res, user, ctx.req);
    return { user: publicUser(user) };
  };
  devLogin.anonymous = true;
  router.post('/api/auth/dev', devLogin);

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
