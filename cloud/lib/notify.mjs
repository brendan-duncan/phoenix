// Telling people what happened that concerns them: something shared with
// them, a group they were added to, a document given to them, a message,
// their account unlocked, an account waiting for an admin. Shown in the
// dashboard (the bell, and the Notifications page); some go by email too.
// Each person chooses, kind by kind, whether to be told in the dashboard
// and by email (users.notify_prefs); a few are always sent.

import { publicUser } from './accounts.mjs';

/**
 * The kinds, with what they are told by default. `always` kinds can't be
 * turned off; `admins` kinds only reach admins.
 */
export const KINDS = {
  share: { label: 'A document is shared with you', inApp: true, email: false },
  transfer: { label: 'A document is given to you', inApp: true, email: true },
  group: { label: 'You are added to a group, or made its owner', inApp: true, email: false },
  message: { label: 'Someone sends you a direct message', inApp: true, email: false },
  chat: { label: 'New messages in your groups\' chats', inApp: false, email: false },
  unlocked: { label: 'Your account is ready', inApp: true, email: true, always: true },
  moderation: { label: 'Moderators flag or remove one of your documents', inApp: true, email: true, always: true },
  waiting: { label: 'An account is waiting to be unlocked', inApp: true, email: false, admins: true },
  report: { label: 'A message or document is reported', inApp: true, email: true, admins: true },
};

/** `user`'s choices for every kind they can get: { kind: { inApp, email } }. */
export function preferences(user) {
  let saved = {};
  try {
    saved = JSON.parse(user.notify_prefs || '{}');
  } catch {
    // Unreadable: the defaults.
  }
  const out = {};
  for (const [kind, info] of Object.entries(KINDS)) {
    if (info.admins && !user.is_admin) {
      continue;
    }
    const mine = saved[kind] ?? {};
    out[kind] = info.always
      ? { inApp: true, email: info.email }
      : { inApp: typeof mine.inApp === 'boolean' ? mine.inApp : info.inApp, email: typeof mine.email === 'boolean' ? mine.email : info.email };
  }
  return out;
}

/** Saves `changes` ({ kind: { inApp?, email? } }) to `user`'s choices. */
export function savePreferences(store, user, changes) {
  let saved = {};
  try {
    saved = JSON.parse(user.notify_prefs || '{}');
  } catch {
    // Start again.
  }
  for (const [kind, change] of Object.entries(changes ?? {})) {
    const info = KINDS[kind];
    if (!info || info.always || (info.admins && !user.is_admin) || !change || typeof change !== 'object') {
      continue;
    }
    saved[kind] = { ...saved[kind] };
    for (const key of ['inApp', 'email']) {
      if (typeof change[key] === 'boolean') {
        saved[kind][key] = change[key];
      }
    }
  }
  store.run('UPDATE users SET notify_prefs = ? WHERE id = ?', JSON.stringify(saved), user.id);
}

/**
 * Notifies each of `userIds` (except the one who did it): { kind, text,
 * link, actorId }, as each of them chose for that kind. `email` ({ subject,
 * text }) is the mail to send; without it the mail is `text` and the link.
 */
export function notify(app, userIds, { kind, text, link = '', actorId = null, email = null }) {
  const { store, mailer, config } = app;
  const now = Date.now();
  for (const userId of new Set(userIds)) {
    if (!userId || userId === actorId) {
      continue;
    }
    const user = store.get("SELECT * FROM users WHERE id = ? AND status = 'active'", userId);
    // Nothing from people they have blocked.
    if (!user || (actorId && store.get('SELECT 1 FROM blocks WHERE blocker_id = ? AND blocked_id = ?', userId, actorId))) {
      continue;
    }
    const wants = preferences(user)[kind] ?? { inApp: true, email: false };
    if (wants.inApp) {
      store.run('INSERT INTO notifications (user_id, kind, text, link, actor_id, created_at) VALUES (?, ?, ?, ?, ?, ?)',
        userId, kind, text, link, actorId, now);
    }
    if (wants.email && user.email) {
      const mail = email ?? { subject: text, text };
      mailer.send({
        to: user.email,
        subject: mail.subject,
        text: `Hello ${user.name},\n\n${mail.text}\n\n${config.publicUrl}${link}\n\nChoose what Phoenix Cloud tells you: ${config.publicUrl}/notifications`,
      });
    }
  }
}

/** The admins (to tell about accounts waiting for them, and reports). */
export function adminIds(store) {
  return store.all("SELECT id FROM users WHERE is_admin = 1 AND status = 'active'").map((u) => u.id);
}

export function unreadCount(store, userId) {
  return store.get('SELECT COUNT(*) AS n FROM notifications WHERE user_id = ? AND read_at IS NULL', userId).n;
}

export function notificationsView(store, userId, limit = 50) {
  const users = new Map();
  const user = (id) => {
    if (id && !users.has(id)) {
      users.set(id, publicUser(store.get('SELECT * FROM users WHERE id = ?', id)));
    }
    return id ? users.get(id) : null;
  };
  return store.all('SELECT * FROM notifications WHERE user_id = ? ORDER BY id DESC LIMIT ?', userId, limit)
    .map((n) => ({ id: n.id, kind: n.kind, text: n.text, link: n.link, actor: user(n.actor_id), createdAt: n.created_at, read: Boolean(n.read_at) }));
}
