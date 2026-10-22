// People: users and their linked sign-ins, sessions, and beta invites.

import { HttpError, parseCookies, serializeCookie } from './http.mjs';
import { hashToken, newId, newToken, sameSecret } from './ids.mjs';

const MINUTE = 60 * 1000;
const DAY = 24 * 60 * MINUTE;
// A session's last-seen time is written at most this often.
const TOUCH_INTERVAL = 5 * MINUTE;

/** What anyone may see of a user. */
export function publicUser(u) {
  return u ? { id: u.id, handle: u.handle, name: u.name, avatarUrl: u.avatar_url } : null;
}

/** What a user sees of themself. */
export function selfUser(u) {
  return { ...publicUser(u), email: u.email, bio: u.bio, isAdmin: Boolean(u.is_admin), createdAt: u.created_at };
}

const HANDLE_RE = /^[a-z0-9][a-z0-9_-]{1,29}$/i;

export function validHandle(handle) {
  return typeof handle === 'string' && HANDLE_RE.test(handle);
}

/** A free handle made from `base` (a name or an email's local part). */
export function makeHandle(store, base) {
  let stem = String(base ?? '').toLowerCase().normalize('NFKD').replace(/[^a-z0-9_-]+/g, '').replace(/^[-_]+/, '').slice(0, 24);
  if (stem.length < 2) {
    stem = 'user';
  }
  for (let n = 0; n < 1000; n++) {
    const handle = n === 0 ? stem : `${stem}${n + 1}`;
    if (!store.get('SELECT 1 FROM users WHERE handle = ?', handle) && !store.get('SELECT 1 FROM groups WHERE handle = ?', handle)) {
      return handle;
    }
  }
  return `${stem}${newId().slice(0, 6).toLowerCase()}`;
}

export function createUser(store, { name, email = null, avatarUrl = '', handle = null, isAdmin = false }) {
  const user = {
    id: newId(),
    handle: handle ?? makeHandle(store, email ? email.split('@')[0] : name),
    name: String(name || handle || 'New user').slice(0, 80),
    email,
    avatar_url: avatarUrl,
    is_admin: isAdmin ? 1 : 0,
    created_at: Date.now(),
  };
  store.run('INSERT INTO users (id, handle, name, email, avatar_url, is_admin, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
    user.id, user.handle, user.name, user.email, user.avatar_url, user.is_admin, user.created_at);
  store.audit(user.id, 'user.create', 'user', user.id, { handle: user.handle, admin: isAdmin });
  return store.get('SELECT * FROM users WHERE id = ?', user.id);
}

export function findUser(store, handleOrEmail) {
  const key = String(handleOrEmail ?? '').trim().replace(/^@/, '');
  if (!key) {
    return null;
  }
  return key.includes('@')
    ? store.get("SELECT * FROM users WHERE email = ? AND status = 'active'", key)
    : store.get("SELECT * FROM users WHERE handle = ? AND status = 'active'", key);
}

// ── Sessions ──────────────────────────────────────────────────────────────

export class Sessions {
  constructor(store, config) {
    this.store = store;
    this.config = config;
    // __Host- cookies must be Secure; plain http://localhost can't use them.
    this.cookieName = config.secureCookies ? '__Host-phoenix_session' : 'phoenix_session';
  }

  create(res, user, req) {
    const token = newToken();
    const now = Date.now();
    const expires = now + this.config.sessionDays * DAY;
    this.store.run('INSERT INTO sessions (id_hash, user_id, csrf, created_at, last_seen_at, expires_at, user_agent) VALUES (?, ?, ?, ?, ?, ?, ?)',
      hashToken(token), user.id, newToken(), now, now, expires, String(req.headers['user-agent'] ?? '').slice(0, 200));
    this.store.run('UPDATE users SET last_seen_at = ? WHERE id = ?', now, user.id);
    res.setHeader('Set-Cookie', serializeCookie(this.cookieName, token, { maxAge: this.config.sessionDays * 86400, secure: this.config.secureCookies }));
  }

  /** The request's { session, user }, or null. Extends a session in use. */
  fromRequest(req) {
    const token = parseCookies(req.headers.cookie)[this.cookieName];
    if (!token) {
      return null;
    }
    const session = this.store.get('SELECT * FROM sessions WHERE id_hash = ?', hashToken(token));
    const now = Date.now();
    if (!session || session.expires_at < now) {
      return null;
    }
    const user = this.store.get('SELECT * FROM users WHERE id = ?', session.user_id);
    if (!user || user.status !== 'active') {
      return null;
    }
    if (now - session.last_seen_at > TOUCH_INTERVAL) {
      this.store.run('UPDATE sessions SET last_seen_at = ?, expires_at = ? WHERE id_hash = ?', now, now + this.config.sessionDays * DAY, session.id_hash);
      this.store.run('UPDATE users SET last_seen_at = ? WHERE id = ?', now, user.id);
    }
    return { session, user };
  }

  checkCsrf(session, header) {
    if (!header || !sameSecret(header, session.csrf)) {
      throw new HttpError(403, 'csrf', 'The page is out of date. Reload it and try again.');
    }
  }

  destroy(res, session) {
    if (session) {
      this.store.run('DELETE FROM sessions WHERE id_hash = ?', session.id_hash);
    }
    res.setHeader('Set-Cookie', serializeCookie(this.cookieName, '', { maxAge: 0, secure: this.config.secureCookies }));
  }

  /** A user's sessions; `id` is a prefix of the stored hash, enough to revoke one. */
  list(userId, current) {
    return this.store.all('SELECT * FROM sessions WHERE user_id = ? AND expires_at > ? ORDER BY last_seen_at DESC', userId, Date.now())
      .map((s) => ({ id: s.id_hash.slice(0, 16), createdAt: s.created_at, lastSeenAt: s.last_seen_at, userAgent: s.user_agent, current: s.id_hash === current?.id_hash }));
  }

  revoke(userId, id) {
    if (!/^[0-9a-f]{16}$/.test(id)) {
      return false;
    }
    return this.store.run('DELETE FROM sessions WHERE user_id = ? AND substr(id_hash, 1, 16) = ?', userId, id).changes > 0;
  }

  purgeExpired() {
    const now = Date.now();
    this.store.run('DELETE FROM sessions WHERE expires_at < ?', now);
    this.store.run('DELETE FROM login_states WHERE created_at < ?', now - this.config.loginStateMinutes * MINUTE);
  }
}

// ── Invites ───────────────────────────────────────────────────────────────

export function createInvite(store, { email = null, note = '', makeAdmin = false, createdBy = null, days = 14 }) {
  const token = newToken();
  const id = newId();
  const now = Date.now();
  store.run('INSERT INTO invites (id, token_hash, email, note, make_admin, created_by, created_at, expires_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
    id, hashToken(token), email || null, String(note).slice(0, 200), makeAdmin, createdBy, now, days ? now + days * DAY : null);
  store.audit(createdBy, 'invite.create', 'invite', id, { email, makeAdmin });
  return { id, token };
}

/** The live invite for `token`, or null. */
export function findInvite(store, token) {
  if (!token) {
    return null;
  }
  const invite = store.get('SELECT * FROM invites WHERE token_hash = ?', hashToken(token));
  if (!invite || invite.claimed_at || invite.revoked_at || (invite.expires_at && invite.expires_at < Date.now())) {
    return null;
  }
  return invite;
}

export function inviteView(i) {
  const now = Date.now();
  let status = 'pending';
  if (i.claimed_at) {
    status = 'claimed';
  } else if (i.revoked_at) {
    status = 'revoked';
  } else if (i.expires_at && i.expires_at < now) {
    status = 'expired';
  }
  return { id: i.id, email: i.email, note: i.note, makeAdmin: Boolean(i.make_admin), createdAt: i.created_at, expiresAt: i.expires_at, claimedBy: i.claimed_by, claimedAt: i.claimed_at, status };
}

// ── Signing in ────────────────────────────────────────────────────────────

/** A sign-in that can't go ahead; `code` picks the message on the sign-in page. */
export class LoginRefused extends Error {
  constructor(code) {
    super(code);
    this.code = code;
  }
}

/**
 * The user a provider's sign-in belongs to, creating or linking one as the
 * settings allow.
 * @param {{ subject: string, email: string|null, emailVerified: boolean, name: string, avatarUrl: string }} profile
 * @param {{ invite?: string|null, linkUserId?: string|null }} state
 */
export function resolveLogin(store, settings, providerId, profile, state) {
  return store.transaction(() => {
    const now = Date.now();
    const email = profile.emailVerified ? profile.email : null;
    const identity = store.get('SELECT * FROM identities WHERE provider = ? AND subject = ?', providerId, profile.subject);
    if (identity) {
      const user = store.get('SELECT * FROM users WHERE id = ?', identity.user_id);
      if (state.linkUserId && state.linkUserId !== user.id) {
        throw new LoginRefused('identity_taken');
      }
      if (user.status !== 'active') {
        throw new LoginRefused('suspended');
      }
      store.run('UPDATE identities SET last_used_at = ?, email = ? WHERE provider = ? AND subject = ?', now, email, providerId, profile.subject);
      if (!user.avatar_url && profile.avatarUrl) {
        store.run('UPDATE users SET avatar_url = ? WHERE id = ?', profile.avatarUrl, user.id);
      }
      return user;
    }

    if (state.linkUserId) {
      const user = store.get('SELECT * FROM users WHERE id = ?', state.linkUserId);
      if (!user || user.status !== 'active') {
        throw new LoginRefused('failed');
      }
      linkIdentity(store, user.id, providerId, profile, email, now);
      store.audit(user.id, 'identity.link', 'user', user.id, { provider: providerId });
      return user;
    }

    // A new sign-in whose email belongs to an existing user is not linked
    // silently: they sign in the way they did before and link it from their
    // account page. (Otherwise anyone who can claim the email at some
    // provider would get the account.)
    if (email && store.get('SELECT 1 FROM users WHERE email = ?', email)) {
      throw new LoginRefused('email_in_use');
    }

    let invite = null;
    if (state.invite) {
      invite = findInvite(store, state.invite);
      if (!invite) {
        throw new LoginRefused('invite_invalid');
      }
      if (invite.email && (!email || invite.email.toLowerCase() !== email.toLowerCase())) {
        throw new LoginRefused('invite_email');
      }
    } else if (settings.signupPolicy === 'closed') {
      throw new LoginRefused('closed');
    } else if (settings.signupPolicy !== 'open') {
      throw new LoginRefused('beta');
    }

    const user = createUser(store, { name: profile.name, email, avatarUrl: profile.avatarUrl, isAdmin: Boolean(invite?.make_admin) });
    linkIdentity(store, user.id, providerId, profile, email, now);
    if (invite) {
      store.run('UPDATE invites SET claimed_by = ?, claimed_at = ? WHERE id = ?', user.id, now, invite.id);
      store.audit(user.id, 'invite.claim', 'invite', invite.id, {});
    }
    return user;
  });
}

function linkIdentity(store, userId, providerId, profile, email, now) {
  store.run('INSERT INTO identities (provider, subject, user_id, email, created_at, last_used_at) VALUES (?, ?, ?, ?, ?, ?)',
    providerId, profile.subject, userId, email, now, now);
}

/** Only same-site paths: a sign-in never redirects somewhere else. */
export function safeReturnPath(value) {
  const s = String(value ?? '/');
  return s.startsWith('/') && !s.startsWith('//') && !s.startsWith('/\\') && !s.startsWith('/api/') ? s.slice(0, 500) : '/';
}
