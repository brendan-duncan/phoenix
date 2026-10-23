// People: users and their passwords and linked sign-ins, sessions,
// registration and its settings, email tokens, and invites.

import { HttpError, parseCookies, serializeCookie } from './http.mjs';
import { hashToken, newId, newToken, sameSecret } from './ids.mjs';
import { hashPassword } from './passwords.mjs';

const MINUTE = 60 * 1000;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;
// A session's last-seen time is written at most this often.
const TOUCH_INTERVAL = 5 * MINUTE;

export const DEFAULT_ADMIN = { name: 'phoenix', password: 'animate' };

/** What anyone may see of a user. */
export function publicUser(u) {
  return u ? { id: u.id, handle: u.handle, name: u.name, avatarUrl: u.avatar_url } : null;
}

/** What a user sees of themself. */
export function selfUser(u) {
  return {
    ...publicUser(u),
    email: u.email,
    emailVerified: Boolean(u.email_verified),
    bio: u.bio,
    isAdmin: Boolean(u.is_admin),
    createdAt: u.created_at,
    hasPassword: Boolean(u.password_hash),
    passwordIsDefault: Boolean(u.password_is_default),
    plan: u.plan,
  };
}

const HANDLE_RE = /^[a-z0-9][a-z0-9_-]{1,29}$/i;
const EMAIL_RE = /^[^@\s]{1,64}@[^@\s]{1,190}\.[^@\s]{1,40}$/;

export function validHandle(handle) {
  return typeof handle === 'string' && HANDLE_RE.test(handle);
}

// A person's name is what others see and what they sign in with, so it is
// unique (whatever the capitals): 2 to 30 letters and digits, with spaces,
// '.', '-', '_' or an apostrophe between them. Never an '@', which marks an
// email address when signing in.
const NAME_RE = /^[\p{L}\p{N}](?:[\p{L}\p{N} ._'-]*[\p{L}\p{N}])?$/u;

/** A name as typed, tidied: trimmed, spaces run together. */
export function normalizeName(name) {
  return String(name ?? '').normalize('NFC').trim().replace(/\s+/g, ' ');
}

const NAME_RULE = "A name is 2 to 30 letters or digits, with spaces, ., -, _ or ' between them.";
const NAME_TAKEN = 'Someone already has that name.';

/** Why `name` can't be a person's name (already normalized), or null. */
export function nameProblem(store, name, exceptUserId = null) {
  if ([...name].length < 2 || [...name].length > 30 || !NAME_RE.test(name)) {
    return NAME_RULE;
  }
  return handleTaken(store, name, exceptUserId) ? NAME_TAKEN : null;
}

/** `name` tidied, or an error: 400 when it breaks the rule, 409 when someone has it. */
export function requireName(store, name, exceptUserId = null) {
  const tidy = normalizeName(name);
  const problem = nameProblem(store, tidy, exceptUserId);
  if (problem === NAME_TAKEN) {
    throw new HttpError(409, 'taken', problem);
  }
  if (problem) {
    throw new HttpError(400, 'bad_request', problem);
  }
  return tidy;
}

/** A free name made from `base` (a provider's name or an email's local part). */
export function freeName(store, base) {
  let stem = normalizeName(base).replace(/[^\p{L}\p{N} ._'-]+/gu, '').replace(/^[^\p{L}\p{N}]+|[^\p{L}\p{N}]+$/gu, '').slice(0, 26);
  if ([...stem].length < 2) {
    stem = 'User';
  }
  for (let n = 1; n < 1000; n++) {
    const name = n === 1 ? stem : `${stem} ${n}`;
    if (!nameProblem(store, name)) {
      return name;
    }
  }
  return `${stem} ${newId().slice(0, 6)}`;
}

export function validEmail(email) {
  return typeof email === 'string' && EMAIL_RE.test(email);
}

export function handleTaken(store, handle, exceptUserId = null) {
  return Boolean(store.get('SELECT 1 FROM users WHERE handle = ? AND id IS NOT ?', handle, exceptUserId) || store.get('SELECT 1 FROM groups WHERE handle = ?', handle));
}

/** A free handle made from `base` (a name or an email's local part). */
export function makeHandle(store, base) {
  let stem = String(base ?? '').toLowerCase().normalize('NFKD').replace(/[^a-z0-9_-]+/g, '').replace(/^[-_]+/, '').slice(0, 24);
  if (stem.length < 2) {
    stem = 'user';
  }
  for (let n = 0; n < 1000; n++) {
    const handle = n === 0 ? stem : `${stem}${n + 1}`;
    if (!handleTaken(store, handle)) {
      return handle;
    }
  }
  return `${stem}${newId().slice(0, 6).toLowerCase()}`;
}

/**
 * A new account. `name` must be free (nameProblem); without one, a free
 * name is made from the email address. The name is also its handle.
 */
export function createUser(store, { name = null, email = null, avatarUrl = '', isAdmin = false, status = 'active', passwordHash = null, passwordIsDefault = false, emailVerified = false, requestMessage = '' }) {
  const id = newId();
  const finalName = name ?? freeName(store, email ? email.split('@')[0] : 'User');
  store.run(`INSERT INTO users (id, handle, name, email, avatar_url, is_admin, status, password_hash, password_is_default, email_verified, request_message, plan, created_at)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
  id, finalName, finalName, email, avatarUrl, isAdmin, status, passwordHash, passwordIsDefault, emailVerified, requestMessage, store.settings.defaultPlan, Date.now());
  store.audit(id, 'user.create', 'user', id, { name: finalName, admin: isAdmin, status });
  return store.get('SELECT * FROM users WHERE id = ?', id);
}

/** An active user by handle or email (for sharing and adding to groups). */
export function findUser(store, handleOrEmail) {
  const user = findAccount(store, handleOrEmail);
  return user?.status === 'active' ? user : null;
}

/** Any account, whatever its status, by handle or email. */
export function findAccount(store, handleOrEmail) {
  const key = String(handleOrEmail ?? '').trim().replace(/^@/, '');
  if (!key) {
    return null;
  }
  return key.includes('@')
    ? store.get('SELECT * FROM users WHERE email = ?', key) ?? null
    : store.get('SELECT * FROM users WHERE handle = ?', key) ?? null;
}

/**
 * On a server with no accounts at all, the admin account @phoenix. In dev
 * its password is "animate"; in production it is PHOENIX_CLOUD_ADMIN_PASSWORD,
 * or a random one printed once, since "animate" is no secret in a public
 * repository and anyone could sign in with it before you.
 */
export async function ensureDefaultAdmin(store, config) {
  if (store.get('SELECT 1 FROM users LIMIT 1')) {
    return null;
  }
  const given = config.mode === 'dev' ? null : config.adminPassword;
  const password = config.mode === 'dev' ? DEFAULT_ADMIN.password : (given ?? newToken().slice(0, 16));
  const user = createUser(store, {
    name: DEFAULT_ADMIN.name,
    isAdmin: true,
    passwordHash: await hashPassword(password),
    passwordIsDefault: !given,
  });
  console.log(given
    ? `[cloud] made the admin account @${DEFAULT_ADMIN.name} with the password in PHOENIX_CLOUD_ADMIN_PASSWORD`
    : `[cloud] made the admin account @${DEFAULT_ADMIN.name} with the password "${password}"; change it after signing in`);
  return user;
}

/** What a newly registered account starts as, under the current settings. */
export function newAccountStatus(settings, { invited = false, emailVerified = false } = {}) {
  if (invited || settings.newAccounts === 'open') {
    return 'active';
  }
  if (settings.newAccounts === 'email') {
    return emailVerified ? 'active' : 'pending_email';
  }
  return 'pending_admin';
}

/** Why an account that is not active can't sign in, for people. */
export const STATUS_MESSAGES = {
  pending_email: 'Confirm your email address first: follow the link in the email we sent.',
  pending_admin: 'Your account is waiting for an admin to unlock it.',
  suspended: 'This account is suspended.',
};

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

  /** Signs a user out everywhere but `keep` (after a password change). */
  endOthers(userId, keep = null) {
    this.store.run('DELETE FROM sessions WHERE user_id = ? AND id_hash IS NOT ?', userId, keep?.id_hash ?? null);
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
    this.store.run('DELETE FROM email_tokens WHERE expires_at < ?', now);
  }
}

// ── Email tokens ──────────────────────────────────────────────────────────

/** A one-use token sent by email (`purpose`: 'confirm'). */
export function makeEmailToken(store, userId, purpose, hours = 48) {
  const token = newToken();
  const now = Date.now();
  store.run('INSERT INTO email_tokens (token_hash, user_id, purpose, created_at, expires_at) VALUES (?, ?, ?, ?, ?)',
    hashToken(token), userId, purpose, now, now + hours * HOUR);
  return token;
}

/** Uses up a token; the user id it was for, or null. */
export function takeEmailToken(store, token, purpose) {
  const row = token ? store.get('SELECT * FROM email_tokens WHERE token_hash = ?', hashToken(token)) : null;
  if (!row || row.purpose !== purpose || row.used_at || row.expires_at < Date.now()) {
    return null;
  }
  store.run('UPDATE email_tokens SET used_at = ? WHERE token_hash = ?', Date.now(), row.token_hash);
  return row.user_id;
}

/** Sends the email that confirms `user`'s address. */
export function sendConfirmation(store, mailer, config, user, { changed = false } = {}) {
  const token = makeEmailToken(store, user.id, 'confirm');
  return mailer.send({
    to: user.email,
    subject: changed ? 'Confirm your new email address for Phoenix Cloud' : 'Confirm your Phoenix Cloud account',
    text: changed
      ? `Hello ${user.name},\n\nYou changed your Phoenix Cloud email address to this one. Confirm it here:\n${config.publicUrl}/confirm/${token}\n\nThe link works for 48 hours. If you did not change it, sign in and change your password.`
      : `Hello ${user.name},\n\nConfirm your email address to start using Phoenix Cloud:\n${config.publicUrl}/confirm/${token}\n\nThe link works for 48 hours. If you did not make this account, ignore this email.`,
  });
}

/** The email that carries an invite's link to the address it was made for. */
export function sendInvite(mailer, config, { email, url, note = '', from = null, days = 14, replyTo = null }) {
  return mailer.send({
    to: email,
    replyTo,
    subject: `${from ? `${from} invited you` : 'You are invited'} to Phoenix Cloud`,
    text: [
      `Hello,\n\n${from ? `${from} invited you` : 'You are invited'} to Phoenix Cloud, where you keep, share and edit Phoenix animation documents in your browser.`,
      note ? `\n"${note}"` : '',
      `\nMake your account here:\n${url}`,
      `\n${days ? `The link works for ${days} day${days === 1 ? '' : 's'}, and once.` : 'The link works once.'} If you were not expecting this, ignore this email.`,
    ].join('\n'),
  });
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

export function claimInvite(store, invite, userId) {
  store.run('UPDATE invites SET claimed_by = ?, claimed_at = ? WHERE id = ?', userId, Date.now(), invite.id);
  if (invite.make_admin) {
    store.run('UPDATE users SET is_admin = 1 WHERE id = ?', userId);
  }
  store.audit(userId, 'invite.claim', 'invite', invite.id, {});
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

// ── Signing in with a provider ────────────────────────────────────────────

/** A sign-in that can't go ahead; `code` picks the message on the sign-in page. */
export class LoginRefused extends Error {
  constructor(code) {
    super(code);
    this.code = code;
  }
}

/**
 * The user a provider's sign-in belongs to, creating or linking one as the
 * settings allow. A new account that has to wait for an admin is made, and
 * the sign-in refused until it is unlocked.
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
        throw new LoginRefused(user.status);
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

    if (!settings.registration && !state.invite) {
      throw new LoginRefused('registration_closed');
    }
    if (!state.invite && state.mayCreate && !state.mayCreate()) {
      throw new LoginRefused('busy');
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
    }
    // A provider's verified email counts as confirmed; without one, an
    // email-confirmed server has nothing to confirm, so an admin decides.
    let status = newAccountStatus(settings, { invited: Boolean(invite), emailVerified: Boolean(email) });
    if (status === 'pending_email') {
      status = 'pending_admin';
    }
    const user = createUser(store, { name: freeName(store, profile.name || (email ?? '').split('@')[0]), email, avatarUrl: profile.avatarUrl, status, emailVerified: Boolean(email) });
    if (!invite) {
      state.created?.();
    }
    linkIdentity(store, user.id, providerId, profile, email, now);
    if (invite) {
      claimInvite(store, invite, user.id);
    }
    if (status !== 'active') {
      return { pending: status, created: user };
    }
    return store.get('SELECT * FROM users WHERE id = ?', user.id);
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

/** Renames a user (the name is also the handle). */
export function renameUser(store, userId, name) {
  store.run('UPDATE users SET name = ?, handle = ? WHERE id = ?', name, name, userId);
}
