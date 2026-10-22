// Messages: conversations (direct, and groups' chats), sending and reading,
// muting and hiding, deleting and reporting; each person's choice of who
// may message them and whom they block; the admins' list of reports.

import { findUser, publicUser } from '../accounts.mjs';
import { badRequest, HttpError, notFound } from '../http.mjs';
import { Budget } from '../limits.mjs';
import {
  blocks, conversationPeople, conversationsOf, deleteMessage, directConversation, DM_POLICIES, groupConversation, inConversation,
  markRead, otherPerson, requireConversation, sendMessage, unreadIn, whyNotMessage,
} from '../messages.mjs';
import { adminIds, notify } from '../notify.mjs';

// Messages one person may send, a minute.
const PER_MINUTE = 30;

function messageView(store, m, viewerId) {
  return {
    id: m.id,
    sender: m.sender_id ? publicUser(store.get('SELECT * FROM users WHERE id = ?', m.sender_id)) : null,
    body: m.deleted_at ? '' : m.body,
    deleted: Boolean(m.deleted_at),
    mine: m.sender_id === viewerId,
    createdAt: m.created_at,
  };
}

function conversationView(store, c, user) {
  const view = { id: c.id, kind: c.kind, muted: Boolean(c.muted), unread: unreadIn(store, c, user.id), lastMessageAt: c.last_message_at };
  if (c.kind === 'dm') {
    const other = otherPerson(store, c, user.id);
    view.other = publicUser(other);
    view.title = other?.name ?? 'Deleted account';
    view.blocked = Boolean(other && blocks(store, user.id, other.id));
  } else if (c.kind === 'group') {
    const group = store.get('SELECT id, name FROM groups WHERE id = ?', c.group_id);
    view.group = group;
    view.title = group?.name ?? 'A group';
  } else {
    const team = store.get('SELECT id, name FROM teams WHERE id = ?', c.team_id);
    view.team = team;
    view.title = team?.name ?? 'A team';
  }
  const last = store.get(
    `SELECT * FROM messages WHERE conversation_id = ? AND (sender_id IS NULL OR sender_id NOT IN (SELECT blocked_id FROM blocks WHERE blocker_id = ?))
     ORDER BY id DESC LIMIT 1`, c.id, user.id);
  view.last = last ? messageView(store, last, user.id) : null;
  return view;
}

/** A person named by id, name or email. */
function someone(store, who) {
  return findUser(store, who) ?? store.get("SELECT * FROM users WHERE id = ? AND status = 'active'", String(who ?? '')) ?? null;
}

function messagingView(store, user) {
  const people = (sql) => store.all(sql, user.id).map(publicUser);
  return {
    policy: DM_POLICIES.includes(user.dm_policy) ? user.dm_policy : 'groups',
    allowed: people('SELECT u.* FROM dm_allowed a JOIN users u ON u.id = a.allowed_id WHERE a.user_id = ? ORDER BY u.name COLLATE NOCASE'),
    blocked: people('SELECT u.* FROM blocks b JOIN users u ON u.id = b.blocked_id WHERE b.blocker_id = ? ORDER BY u.name COLLATE NOCASE'),
  };
}

export function registerMessages(router, app) {
  const { store } = app;
  const sending = new Budget(PER_MINUTE, 60 * 1000);

  router.get('/api/conversations', (ctx) => {
    const user = ctx.requireUser();
    return { conversations: conversationsOf(store, user).map((c) => conversationView(store, c, user)) };
  });

  // Starts (or finds) the direct conversation with { to }.
  router.post('/api/conversations', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    const to = someone(store, body.to);
    if (!to) {
      throw notFound('No one with an account has that name or email.');
    }
    const existing = store.get('SELECT * FROM conversations WHERE dm_key = ?', [user.id, to.id].sort().join(':'));
    const theyWrote = existing && store.get('SELECT 1 FROM messages WHERE conversation_id = ? AND sender_id = ? LIMIT 1', existing.id, to.id);
    const why = theyWrote ? null : whyNotMessage(store, user, to);
    if (why) {
      throw new HttpError(403, 'not_taking', why);
    }
    const conversation = directConversation(store, user.id, to.id);
    store.run('UPDATE conversation_people SET hidden = 0 WHERE conversation_id = ? AND user_id = ?', conversation.id, user.id);
    return { conversation: conversationView(store, { ...conversation, muted: 0 }, user) };
  });

  // Whether `to` takes messages from the asker, as they type the name.
  router.get('/api/messaging/check', (ctx) => {
    const user = ctx.requireUser();
    const to = someone(store, ctx.query('to'));
    const why = to ? whyNotMessage(store, user, to) : 'No one with an account has that name or email.';
    return { ok: !why, why, to: publicUser(to) };
  });

  // A group's chat.
  router.get('/api/groups/:id/chat', (ctx) => {
    const user = ctx.requireUser();
    if (!store.get('SELECT 1 FROM group_members WHERE group_id = ? AND user_id = ?', ctx.params.id, user.id)) {
      throw notFound('No such group, or you are not in it.');
    }
    const conversation = groupConversation(store, ctx.params.id);
    return { conversation: conversationView(store, conversation, user) };
  });

  router.get('/api/conversations/:id', (ctx) => {
    const user = ctx.requireUser();
    const conversation = requireConversation(store, user, ctx.params.id);
    const row = store.get('SELECT muted, hidden FROM conversation_people WHERE conversation_id = ? AND user_id = ?', conversation.id, user.id);
    const view = conversationView(store, { ...conversation, muted: row?.muted ?? 0 }, user);
    if (conversation.kind === 'dm') {
      const other = otherPerson(store, conversation, user.id);
      const theyWrote = other && store.get('SELECT 1 FROM messages WHERE conversation_id = ? AND sender_id = ? AND deleted_at IS NULL LIMIT 1', conversation.id, other.id);
      view.cannotSend = !other ? 'They no longer have an account here.'
        : blocks(store, user.id, other.id) ? 'You have blocked them.'
          : blocks(store, other.id, user.id) ? `${other.name} is not taking messages from you.`
            : theyWrote ? null : whyNotMessage(store, user, other);
    } else {
      view.members = conversationPeople(store, conversation).length;
    }
    return { conversation: view };
  });

  // Messages, oldest first: the latest `limit`, or those ?after= an id
  // (new ones) or ?before= one (older ones). Not from people the asker blocked.
  router.get('/api/conversations/:id/messages', (ctx) => {
    const user = ctx.requireUser();
    const conversation = requireConversation(store, user, ctx.params.id);
    const limit = Math.min(Math.max(Number(ctx.query('limit')) || 50, 1), 200);
    const after = Number(ctx.query('after')) || 0;
    const before = Number(ctx.query('before')) || 0;
    const notBlocked = '(sender_id IS NULL OR sender_id NOT IN (SELECT blocked_id FROM blocks WHERE blocker_id = ?))';
    let rows;
    if (after) {
      rows = store.all(`SELECT * FROM messages WHERE conversation_id = ? AND id > ? AND ${notBlocked} ORDER BY id LIMIT ?`, conversation.id, after, user.id, limit);
    } else {
      rows = store.all(`SELECT * FROM messages WHERE conversation_id = ? ${before ? 'AND id < ?' : ''} AND ${notBlocked} ORDER BY id DESC LIMIT ?`,
        ...[conversation.id, ...(before ? [before] : []), user.id, limit]).reverse();
    }
    return { messages: rows.map((m) => messageView(store, m, user.id)), more: !after && rows.length === limit };
  });

  router.post('/api/conversations/:id/messages', async (ctx) => {
    const user = ctx.requireUser();
    const conversation = requireConversation(store, user, ctx.params.id);
    if (!sending.take(user.id)) {
      throw new HttpError(429, 'slow_down', 'That is a lot of messages at once. Wait a moment.');
    }
    const body = await ctx.json();
    // Who had nothing unread before this, to be told once, not per message.
    const others = conversationPeople(store, conversation).filter((id) => id !== user.id);
    const quiet = others.filter((id) => unreadIn(store, conversation, id) === 0 &&
      !store.get('SELECT 1 FROM conversation_people WHERE conversation_id = ? AND user_id = ? AND muted = 1', conversation.id, id));
    const id = sendMessage(store, user, conversation, body.body);
    if (conversation.kind === 'dm') {
      notify(app, quiet, { kind: 'message', text: `${user.name} sent you a message`, link: `/messages/${conversation.id}`, actorId: user.id });
    } else if (conversation.kind === 'group') {
      const group = store.get('SELECT name FROM groups WHERE id = ?', conversation.group_id);
      notify(app, quiet, { kind: 'chat', text: `${user.name} wrote in ${group?.name ?? 'a group'}`, link: `/messages/${conversation.id}`, actorId: user.id });
    }
    ctx.status = 201;
    return { message: messageView(store, store.get('SELECT * FROM messages WHERE id = ?', id), user.id) };
  });

  router.post('/api/conversations/:id/read', async (ctx) => {
    const user = ctx.requireUser();
    const conversation = requireConversation(store, user, ctx.params.id);
    const body = await ctx.json();
    markRead(store, conversation, user.id, Number.isInteger(body.upTo) ? body.upTo : null);
    return { unread: unreadIn(store, conversation, user.id) };
  });

  // { muted }, and for a direct one { hidden } (it comes back with a new message).
  router.patch('/api/conversations/:id', async (ctx) => {
    const user = ctx.requireUser();
    const conversation = requireConversation(store, user, ctx.params.id);
    const body = await ctx.json();
    store.run('INSERT OR IGNORE INTO conversation_people (conversation_id, user_id) VALUES (?, ?)', conversation.id, user.id);
    if (typeof body.muted === 'boolean') {
      store.run('UPDATE conversation_people SET muted = ? WHERE conversation_id = ? AND user_id = ?', body.muted ? 1 : 0, conversation.id, user.id);
    }
    if (typeof body.hidden === 'boolean' && conversation.kind === 'dm') {
      store.run('UPDATE conversation_people SET hidden = ? WHERE conversation_id = ? AND user_id = ?', body.hidden ? 1 : 0, conversation.id, user.id);
    }
    return { ok: true };
  });

  router.delete('/api/messages/:id', (ctx) => {
    const user = ctx.requireUser();
    const message = store.get('SELECT * FROM messages WHERE id = ?', Number(ctx.params.id));
    // Someone outside the conversation learns nothing about it.
    if (!message || (!user.is_admin && !inConversation(store, user, store.get('SELECT * FROM conversations WHERE id = ?', message.conversation_id)))) {
      throw notFound('No such message.');
    }
    deleteMessage(store, user, message.id);
    if (message.sender_id !== user.id) {
      store.audit(user.id, 'message.delete', 'message', String(message.id), { sender: message.sender_id });
    }
    return { ok: true };
  });

  // Reports a message to the admins: { reason }.
  router.post('/api/messages/:id/report', async (ctx) => {
    const user = ctx.requireUser();
    const message = store.get('SELECT * FROM messages WHERE id = ?', Number(ctx.params.id));
    if (!message || !inConversation(store, user, store.get('SELECT * FROM conversations WHERE id = ?', message.conversation_id))) {
      throw notFound('No such message.');
    }
    if (message.sender_id === user.id) {
      throw badRequest('That is your own message.');
    }
    if (store.get('SELECT 1 FROM message_reports WHERE message_id = ? AND reporter_id = ? AND resolved_at IS NULL', message.id, user.id)) {
      return { ok: true };
    }
    const body = await ctx.json();
    store.run('INSERT INTO message_reports (message_id, reporter_id, reason, created_at) VALUES (?, ?, ?, ?)',
      message.id, user.id, String(body.reason ?? '').trim().slice(0, 1000), Date.now());
    notify(app, adminIds(store), { kind: 'report', text: `${user.name} reported a message`, link: '/admin/reports', actorId: user.id });
    ctx.status = 201;
    return { ok: true };
  });

  // ── Who may message whom ────────────────────────────────────────────────

  router.get('/api/me/messaging', (ctx) => messagingView(store, ctx.requireUser()));

  router.patch('/api/me/messaging', async (ctx) => {
    const user = ctx.requireUser();
    const body = await ctx.json();
    if (!DM_POLICIES.includes(body.policy)) {
      throw badRequest(`Who may message you is one of ${DM_POLICIES.join(', ')}.`);
    }
    store.run('UPDATE users SET dm_policy = ? WHERE id = ?', body.policy, user.id);
    return messagingView(store, store.get('SELECT * FROM users WHERE id = ?', user.id));
  });

  const listRoutes = (path, table, mine, theirs, verb) => {
    router.put(`/api/me/${path}/:who`, (ctx) => {
      const user = ctx.requireUser();
      const other = someone(store, ctx.params.who);
      if (!other) {
        throw notFound('No one with an account has that name.');
      }
      if (other.id === user.id) {
        throw badRequest('That is you.');
      }
      store.run(`INSERT OR IGNORE INTO ${table} (${mine}, ${theirs}, created_at) VALUES (?, ?, ?)`, user.id, other.id, Date.now());
      store.audit(user.id, `user.${verb}`, 'user', other.id);
      return messagingView(store, user);
    });
    router.delete(`/api/me/${path}/:who`, (ctx) => {
      const user = ctx.requireUser();
      const other = someone(store, ctx.params.who);
      if (other) {
        store.run(`DELETE FROM ${table} WHERE ${mine} = ? AND ${theirs} = ?`, user.id, other.id);
      }
      return messagingView(store, user);
    });
  };
  listRoutes('allowed', 'dm_allowed', 'user_id', 'allowed_id', 'allow_messages');
  listRoutes('blocks', 'blocks', 'blocker_id', 'blocked_id', 'block');

  // ── Reports, for admins ─────────────────────────────────────────────────

  router.get('/api/admin/reports', (ctx) => {
    const admin = ctx.requireAdmin();
    const open = ctx.query('all') !== '1';
    const rows = store.all(
      `SELECT r.*, m.body, m.sender_id, m.created_at AS sent_at, m.deleted_at, m.conversation_id, c.kind
       FROM message_reports r JOIN messages m ON m.id = r.message_id JOIN conversations c ON c.id = m.conversation_id
       ${open ? 'WHERE r.resolved_at IS NULL' : ''} ORDER BY r.created_at DESC LIMIT 200`);
    const person = (id) => (id ? publicUser(store.get('SELECT * FROM users WHERE id = ?', id)) : null);
    return {
      reports: rows.map((r) => ({
        id: r.id,
        reason: r.reason,
        createdAt: r.created_at,
        reporter: person(r.reporter_id),
        resolvedAt: r.resolved_at,
        outcome: r.outcome,
        message: { id: r.message_id, body: r.deleted_at ? '' : r.body, deleted: Boolean(r.deleted_at), sender: person(r.sender_id), createdAt: r.sent_at, kind: r.kind },
        // A few messages before it, for what it answered.
        context: store.all('SELECT * FROM messages WHERE conversation_id = ? AND id < ? ORDER BY id DESC LIMIT 3', r.conversation_id, r.message_id)
          .reverse().map((m) => messageView(store, m, admin.id)),
      })),
    };
  });

  // { outcome: 'dismissed' | 'removed' }: removing deletes the message.
  router.patch('/api/admin/reports/:id', async (ctx) => {
    const admin = ctx.requireAdmin();
    const report = store.get('SELECT * FROM message_reports WHERE id = ?', Number(ctx.params.id));
    if (!report) {
      throw notFound('No such report.');
    }
    const body = await ctx.json();
    if (!['dismissed', 'removed'].includes(body.outcome)) {
      throw badRequest('A report is dismissed, or its message removed.');
    }
    if (body.outcome === 'removed') {
      const message = store.get('SELECT * FROM messages WHERE id = ?', report.message_id);
      if (message && !message.deleted_at) {
        deleteMessage(store, admin, message.id);
      }
    }
    // Every open report of the same message is settled with it.
    store.run('UPDATE message_reports SET resolved_at = ?, resolved_by = ?, outcome = ? WHERE message_id = ? AND resolved_at IS NULL',
      Date.now(), admin.id, body.outcome, report.message_id);
    store.audit(admin.id, `report.${body.outcome}`, 'message', String(report.message_id), { report: report.id });
    return { ok: true };
  });
}

