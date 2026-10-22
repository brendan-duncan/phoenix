// Messages: direct ones between two people, and a chat for each group (and
// each team, once there are teams). A conversation's people are its two
// people for a direct one (conversation_people), and the group's or team's
// members for the others; conversation_people also keeps where each person
// has read to, and whether they muted or hid it.
//
// Who may start a direct conversation with whom is the recipient's choice
// (users.dm_policy): nobody, only people they chose (dm_allowed), people in
// a team with them, people in a group (or team) with them, or anyone. The
// people they chose may always write. Once someone has written in a
// conversation, the other may answer. A block (blocks) stops messages both
// ways and hides the blocked person's messages in group chats; reports
// (message_reports) go to the admins.

import { HttpError, badRequest, forbidden, notFound } from './http.mjs';
import { newId } from './ids.mjs';

export const DM_POLICIES = ['none', 'chosen', 'team', 'groups', 'everyone'];
export const MAX_MESSAGE = 4000;

/** Whether `a` has blocked `b`. */
export function blocks(store, a, b) {
  return Boolean(store.get('SELECT 1 FROM blocks WHERE blocker_id = ? AND blocked_id = ?', a, b));
}

function shareTeam(store, a, b) {
  return Boolean(store.get(
    'SELECT 1 FROM team_members x JOIN team_members y ON x.team_id = y.team_id WHERE x.user_id = ? AND y.user_id = ? LIMIT 1', a, b));
}

function shareGroup(store, a, b) {
  return Boolean(store.get(
    'SELECT 1 FROM group_members x JOIN group_members y ON x.group_id = y.group_id WHERE x.user_id = ? AND y.user_id = ? LIMIT 1', a, b));
}

/**
 * Why `from` may not start a direct conversation with `to`, or null when
 * they may.
 */
export function whyNotMessage(store, from, to) {
  if (!to || to.status !== 'active') {
    return 'They do not have an account here.';
  }
  if (to.id === from.id) {
    return 'That is you.';
  }
  if (blocks(store, from.id, to.id)) {
    return 'You have blocked them. Unblock them to send a message.';
  }
  if (blocks(store, to.id, from.id)) {
    return `${to.name} is not taking messages from you.`;
  }
  const policy = DM_POLICIES.includes(to.dm_policy) ? to.dm_policy : 'groups';
  if (policy === 'none') {
    return `${to.name} is not taking direct messages.`;
  }
  if (store.get('SELECT 1 FROM dm_allowed WHERE user_id = ? AND allowed_id = ?', to.id, from.id)) {
    return null;
  }
  const allowed = policy === 'everyone'
    || (policy === 'groups' && (shareGroup(store, from.id, to.id) || shareTeam(store, from.id, to.id)))
    || (policy === 'team' && shareTeam(store, from.id, to.id));
  return allowed ? null : `${to.name} takes direct messages only from ${{
    chosen: 'people they chose',
    team: 'people in their team',
    groups: 'people in a group with them',
  }[policy]}.`;
}

/** The direct conversation between `a` and `b`, made if there is none. */
export function directConversation(store, a, b) {
  const key = [a, b].sort().join(':');
  let conversation = store.get('SELECT * FROM conversations WHERE dm_key = ?', key);
  if (!conversation) {
    const id = newId();
    store.transaction(() => {
      store.run("INSERT INTO conversations (id, kind, dm_key, created_at) VALUES (?, 'dm', ?, ?)", id, key, Date.now());
      for (const userId of [a, b]) {
        store.run('INSERT INTO conversation_people (conversation_id, user_id) VALUES (?, ?)', id, userId);
      }
    });
    conversation = store.get('SELECT * FROM conversations WHERE id = ?', id);
  }
  return conversation;
}

/** The chat of `groupId`, made if there is none. */
export function groupConversation(store, groupId) {
  let conversation = store.get('SELECT * FROM conversations WHERE group_id = ?', groupId);
  if (!conversation) {
    const id = newId();
    store.run("INSERT INTO conversations (id, kind, group_id, created_at) VALUES (?, 'group', ?, ?)", id, groupId, Date.now());
    conversation = store.get('SELECT * FROM conversations WHERE id = ?', id);
  }
  return conversation;
}

/** Whether `user` is one of `conversation`'s people. */
export function inConversation(store, user, conversation) {
  if (!conversation || !user) {
    return false;
  }
  if (conversation.kind === 'dm') {
    return Boolean(store.get('SELECT 1 FROM conversation_people WHERE conversation_id = ? AND user_id = ?', conversation.id, user.id));
  }
  if (conversation.kind === 'group') {
    return Boolean(store.get('SELECT 1 FROM group_members WHERE group_id = ? AND user_id = ?', conversation.group_id, user.id));
  }
  if (conversation.kind === 'team') {
    return Boolean(store.get('SELECT 1 FROM team_members WHERE team_id = ? AND user_id = ?', conversation.team_id, user.id));
  }
  return false;
}

/** The conversation `id`, if `user` is in it; else a 404. */
export function requireConversation(store, user, id) {
  const conversation = store.get('SELECT * FROM conversations WHERE id = ?', id);
  if (!inConversation(store, user, conversation)) {
    throw notFound('No such conversation.');
  }
  return conversation;
}

/** The ids of `conversation`'s people. */
export function conversationPeople(store, conversation) {
  const sql = {
    dm: 'SELECT user_id FROM conversation_people WHERE conversation_id = ?',
    group: 'SELECT user_id FROM group_members WHERE group_id = ?',
    team: 'SELECT user_id FROM team_members WHERE team_id = ?',
  }[conversation.kind];
  const key = { dm: conversation.id, group: conversation.group_id, team: conversation.team_id }[conversation.kind];
  return store.all(sql, key).map((r) => r.user_id);
}

/** The other person in a direct conversation. */
export function otherPerson(store, conversation, userId) {
  return store.get(
    `SELECT u.* FROM conversation_people p JOIN users u ON u.id = p.user_id
     WHERE p.conversation_id = ? AND p.user_id != ?`, conversation.id, userId) ?? null;
}

/** `user`'s row for `conversation` in conversation_people, made if missing. */
function personRow(store, conversation, userId) {
  store.run('INSERT OR IGNORE INTO conversation_people (conversation_id, user_id) VALUES (?, ?)', conversation.id, userId);
  return store.get('SELECT * FROM conversation_people WHERE conversation_id = ? AND user_id = ?', conversation.id, userId);
}

/**
 * Adds `body` from `user` to `conversation`; the message's id. Refuses a
 * direct message the other person won't take (see the note above).
 */
export function sendMessage(store, user, conversation, body) {
  const text = String(body ?? '').replace(/\r\n/g, '\n').trim();
  if (!text) {
    throw badRequest('A message needs some words.');
  }
  if (text.length > MAX_MESSAGE) {
    throw badRequest(`A message is at most ${MAX_MESSAGE} characters.`);
  }
  if (conversation.kind === 'dm') {
    const other = otherPerson(store, conversation, user.id);
    // They wrote here first: an answer is always welcome, blocks aside.
    const theyWrote = other && store.get('SELECT 1 FROM messages WHERE conversation_id = ? AND sender_id = ? AND deleted_at IS NULL LIMIT 1', conversation.id, other.id);
    const why = !other ? 'They no longer have an account here.'
      : blocks(store, user.id, other.id) ? 'You have blocked them. Unblock them to send a message.'
        : blocks(store, other.id, user.id) ? `${other.name} is not taking messages from you.`
          : theyWrote ? null : whyNotMessage(store, user, other);
    if (why) {
      throw new HttpError(403, 'not_taking', why);
    }
  }
  const now = Date.now();
  const id = store.transaction(() => {
    const { lastInsertRowid } = store.run('INSERT INTO messages (conversation_id, sender_id, body, created_at) VALUES (?, ?, ?, ?)',
      conversation.id, user.id, text, now);
    store.run('UPDATE conversations SET last_message_at = ? WHERE id = ?', now, conversation.id);
    // The sender has read their own; a hidden conversation comes back.
    personRow(store, conversation, user.id);
    store.run('UPDATE conversation_people SET last_read_id = ?, hidden = 0 WHERE conversation_id = ? AND user_id = ?', Number(lastInsertRowid), conversation.id, user.id);
    if (conversation.kind === 'dm') {
      store.run('UPDATE conversation_people SET hidden = 0 WHERE conversation_id = ?', conversation.id);
    }
    return Number(lastInsertRowid);
  });
  return id;
}

/** Messages `viewerId` has not read in `conversation` (not their own, nor from people they blocked). */
export function unreadIn(store, conversation, viewerId) {
  const row = store.get('SELECT last_read_id FROM conversation_people WHERE conversation_id = ? AND user_id = ?', conversation.id, viewerId);
  return store.get(
    `SELECT COUNT(*) AS n FROM messages WHERE conversation_id = ? AND id > ? AND deleted_at IS NULL
       AND (sender_id IS NULL OR sender_id != ?)
       AND (sender_id IS NULL OR sender_id NOT IN (SELECT blocked_id FROM blocks WHERE blocker_id = ?))`,
    conversation.id, row?.last_read_id ?? 0, viewerId, viewerId).n;
}

/** Marks `conversation` read by `userId` up to message `upTo` (or all). */
export function markRead(store, conversation, userId, upTo = null) {
  const last = upTo ?? store.get('SELECT MAX(id) AS id FROM messages WHERE conversation_id = ?', conversation.id).id ?? 0;
  personRow(store, conversation, userId);
  store.run('UPDATE conversation_people SET last_read_id = MAX(last_read_id, ?) WHERE conversation_id = ? AND user_id = ?', last, conversation.id, userId);
}

/** The conversations `user` is in: direct ones not hidden, and their groups' and teams' chats. */
export function conversationsOf(store, user) {
  for (const g of store.all('SELECT group_id FROM group_members WHERE user_id = ?', user.id)) {
    groupConversation(store, g.group_id);
  }
  return store.all(
    `SELECT c.*, p.muted, p.hidden FROM conversations c
     LEFT JOIN conversation_people p ON p.conversation_id = c.id AND p.user_id = ?
     WHERE (c.kind = 'dm' AND p.user_id IS NOT NULL AND p.hidden = 0)
        OR (c.kind = 'group' AND c.group_id IN (SELECT group_id FROM group_members WHERE user_id = ?))
        OR (c.kind = 'team' AND c.team_id IN (SELECT team_id FROM team_members WHERE user_id = ?))
     ORDER BY COALESCE(c.last_message_at, c.created_at) DESC`, user.id, user.id, user.id);
}

/** How many messages `user` has not read, in conversations they have not muted. */
export function unreadMessages(store, user) {
  let n = 0;
  for (const conversation of conversationsOf(store, user)) {
    if (!conversation.muted) {
      n += unreadIn(store, conversation, user.id);
    }
  }
  return n;
}

/** Deletes message `id` (its words go; that it was there stays): the sender's, or an admin's to do. */
export function deleteMessage(store, user, id) {
  const message = store.get('SELECT * FROM messages WHERE id = ?', id);
  if (!message || message.deleted_at) {
    throw notFound('No such message.');
  }
  if (message.sender_id !== user.id && !user.is_admin) {
    throw forbidden('You can delete only your own messages.');
  }
  store.run("UPDATE messages SET body = '', deleted_at = ? WHERE id = ?", Date.now(), id);
  return message;
}
