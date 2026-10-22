// Telling people what happened that concerns them: something shared with
// them, a group they were added to, their account unlocked, an account
// waiting for an admin. Shown in the dashboard; some go by email too.

import { publicUser } from './accounts.mjs';

/**
 * Notifies each of `userIds` (except the one who did it): { kind, text,
 * link, actorId }. With `email` ({ subject, text }), the people with an
 * email address get it by mail too.
 */
export function notify(app, userIds, { kind, text, link = '', actorId = null, email = null }) {
  const { store, mailer, config } = app;
  const now = Date.now();
  for (const userId of new Set(userIds)) {
    if (!userId || userId === actorId) {
      continue;
    }
    store.run('INSERT INTO notifications (user_id, kind, text, link, actor_id, created_at) VALUES (?, ?, ?, ?, ?, ?)',
      userId, kind, text, link, actorId, now);
    if (email) {
      const user = store.get('SELECT name, email FROM users WHERE id = ?', userId);
      if (user?.email) {
        mailer.send({ to: user.email, subject: email.subject, text: `Hello ${user.name},\n\n${email.text}\n\n${config.publicUrl}${link}` });
      }
    }
  }
}

/** The admins (to tell about accounts waiting for them). */
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
