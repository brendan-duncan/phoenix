// Messages: the list of conversations (direct ones, and each group's chat)
// beside the open one, with new messages fetched every few seconds while
// the page is showing. Also the Notifications page, and the two settings
// panels for the Account page: who may message you, and whom you blocked.

import { del, get, patch, post, put } from './api.js';
import { navigate, rerender, showUnreadMessages, state } from './app.js';
import {
  avatar, clear, confirmDialog, formDialog, h, icon, person, popup, promptDialog, showError, timeAgo, toast,
} from './ui.js';

const THREAD_EVERY_MS = 4000;
const LIST_EVERY_MS = 12000;
// A sender's messages this close together share one heading.
const RUN_MS = 5 * 60 * 1000;

/** Words with their web addresses as links (only http(s), and our own pages). */
function linked(text) {
  const out = [];
  const re = /(https?:\/\/[^\s<]+[^\s<.,;:!?)\]'"])|(\/(?:d|groups|messages)\/[A-Za-z0-9_-]+)/g;
  let at = 0;
  for (const m of text.matchAll(re)) {
    out.push(text.slice(at, m.index));
    const href = m[0];
    out.push(h('a', { class: 'link', href, ...(href.startsWith('http') ? { target: '_blank', rel: 'noopener noreferrer' } : {}) }, href));
    at = m.index + href.length;
  }
  out.push(text.slice(at));
  return out;
}

function picture(c) {
  return c.kind === 'dm' && c.other ? avatar(c.other) : h('span', { class: 'avatar' }, icon('users'));
}

function preview(c) {
  if (!c.last) {
    return c.kind === 'dm' ? 'No messages yet' : 'The group\'s chat';
  }
  const who = c.last.mine ? 'You: ' : (c.kind === 'dm' ? '' : `${c.last.sender?.name ?? 'Someone'}: `);
  return c.last.deleted ? `${who}message deleted` : `${who}${c.last.body.split('\n')[0]}`;
}

/** Starts a direct conversation, saying as the name is typed whether they take messages. */
export async function newMessageDialog(to = '') {
  const who = h('input', { type: 'text', name: 'to', value: to, required: true, placeholder: 'Name or email', autocomplete: 'off', autofocus: true });
  const hint = h('div', { class: 'hint' });
  let ok = false;
  let timer = null;
  const check = () => {
    clearTimeout(timer);
    ok = false;
    who.form?.dispatchEvent(new Event('recheck'));
    const name = who.value.trim();
    if (!name) {
      clear(hint);
      return;
    }
    timer = setTimeout(async () => {
      try {
        const result = await get(`/api/messaging/check?to=${encodeURIComponent(name)}`);
        if (who.value.trim() !== name) {
          return;
        }
        ok = result.ok;
        hint.className = `hint ${ok ? 'good' : 'bad'}`;
        hint.textContent = ok ? `You can message ${result.to.name}.` : result.why;
        who.form?.dispatchEvent(new Event('recheck'));
      } catch {
        // Checked again as they type.
      }
    }, 250);
  };
  who.addEventListener('input', check);
  if (to) {
    setTimeout(check, 0);
  }
  const result = await formDialog({
    title: 'New message',
    submit: 'Start',
    fields: h('label', { class: 'field' }, h('span', {}, 'To'), who, hint),
    valid: () => ok,
    onSubmit: (data) => post('/api/conversations', { to: data.to.trim() }),
  });
  if (result?.conversation) {
    navigate(`/messages/${result.conversation.id}`);
  }
}

/** A group's chat: its conversation, found and opened. */
export async function openGroupChat(groupId) {
  try {
    const { conversation } = await get(`/api/groups/${groupId}/chat`);
    navigate(`/messages/${conversation.id}`);
  } catch (err) {
    showError(err);
  }
}

export async function renderMessages(target, conversationId) {
  const list = h('div', { class: 'convos' });
  const side = h('aside', { class: 'convo-side' },
    h('div', { class: 'convo-head' }, h('h1', {}, 'Messages'),
      h('button', { type: 'button', class: 'btn primary', onclick: () => newMessageDialog().catch(showError) }, icon('plus'), 'New')),
    list);
  const thread = h('section', { class: 'thread' });
  const page = h('div', { class: `messages${conversationId ? ' open' : ''}` }, side, thread);
  clear(target, page);

  let conversations = [];
  const drawList = () => {
    clear(list, conversations.length ? conversations.map((c) => h('a', {
      class: `convo${c.id === conversationId ? ' active' : ''}${c.unread && !c.muted ? ' unread' : ''}`,
      href: `/messages/${c.id}`,
    },
    picture(c),
    h('div', { class: 'convo-text' },
      h('div', { class: 'convo-title' }, h('span', { class: 'grow' }, c.title), c.lastMessageAt ? h('span', { class: 'muted small' }, timeAgo(c.lastMessageAt)) : null),
      h('div', { class: 'muted small convo-last' }, c.muted ? '🔕 ' : '', preview(c))),
    c.unread && !c.muted ? h('span', { class: 'count' }, String(c.unread)) : null))
      : h('div', { class: 'muted small convo-empty' }, 'No conversations yet. Start one with New, or open a group\'s chat.'));
  };
  const loadList = async () => {
    ({ conversations } = await get('/api/conversations'));
    if (page.isConnected) {
      drawList();
      showUnreadMessages(conversations.reduce((n, c) => n + (c.muted ? 0 : c.unread), 0));
    }
  };
  await loadList();

  if (!conversationId) {
    clear(thread, h('div', { class: 'empty' }, icon('users'), h('h2', {}, 'Choose a conversation'),
      h('p', {}, 'Or start a new one. Each of your groups has a chat of its own.')));
  } else {
    await openThread(thread, conversationId, () => loadList().catch(() => {}));
  }

  // Fresh while the page shows; stops when it is left.
  const timer = setInterval(() => {
    if (!page.isConnected) {
      clearInterval(timer);
      return;
    }
    if (document.visibilityState === 'visible') {
      loadList().catch(() => {});
    }
  }, LIST_EVERY_MS);
}

async function openThread(thread, id, onChange) {
  let conversation;
  try {
    ({ conversation } = await get(`/api/conversations/${id}`));
  } catch (err) {
    clear(thread, h('div', { class: 'empty' }, h('h2', {}, 'No such conversation'), h('p', {}, err.message)));
    return;
  }
  const scroller = h('div', { class: 'thread-messages' });
  const older = h('button', { type: 'button', class: 'btn ghost small-btn hidden' }, 'Older messages');
  const box = h('textarea', { rows: 1, maxlength: 4000, placeholder: `Message ${conversation.title}`, 'aria-label': 'Message' });
  const sendButton = h('button', { type: 'submit', class: 'btn primary', 'aria-label': 'Send' }, 'Send');
  const composer = h('form', { class: 'composer' }, box, sendButton);
  const blocked = h('div', { class: 'muted small composer-note hidden' });
  const setCannot = (why) => {
    blocked.textContent = why ?? '';
    blocked.classList.toggle('hidden', !why);
    composer.classList.toggle('hidden', Boolean(why));
  };
  setCannot(conversation.cannotSend);

  let first = 0;
  let last = 0;
  let lastSender = null;
  let lastAt = 0;
  const messageEl = (m) => {
    const sameRun = m.sender?.id === lastSender && m.createdAt - lastAt < RUN_MS;
    lastSender = m.sender?.id ?? null;
    lastAt = m.createdAt;
    const actions = h('button', {
      type: 'button', class: 'btn ghost icon-only msg-more', 'aria-label': 'More', onclick: (e) => popup(e.currentTarget, m.mine || state.me.user.isAdmin
        ? [{
          label: 'Delete', icon: 'trash', onclick: async () => {
            if (await confirmDialog({ title: 'Delete this message?', message: 'Its words are gone for everyone.', confirm: 'Delete', danger: true })) {
              await del(`/api/messages/${m.id}`).catch(showError);
              el.replaceWith(messageEl({ ...m, deleted: true, body: '' }));
            }
          },
        }, ...(m.mine ? [] : [reportItem(m)])]
        : [reportItem(m)]),
    }, icon('more'));
    const el = h('div', { class: `msg${sameRun ? ' run' : ''}${m.mine ? ' mine' : ''}`, dataset: { id: String(m.id) } },
      sameRun ? h('span', { class: 'msg-gap' }) : (m.sender ? avatar(m.sender) : h('span', { class: 'avatar' }, '?')),
      h('div', { class: 'msg-body' },
        sameRun ? null : h('div', { class: 'msg-head' }, h('strong', {}, m.sender?.name ?? 'Deleted account'), h('span', { class: 'muted small' }, timeAgo(m.createdAt))),
        m.deleted ? h('div', { class: 'muted small msg-text' }, 'Message deleted') : h('div', { class: 'msg-text' }, linked(m.body))),
      m.deleted ? null : actions);
    return el;
  };
  const atBottom = () => scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 60;
  const append = (messages) => {
    const stick = atBottom();
    for (const m of messages) {
      scroller.append(messageEl(m));
      last = Math.max(last, m.id);
    }
    if (stick || messages.some((m) => m.mine)) {
      scroller.scrollTop = scroller.scrollHeight;
    }
  };
  const markRead = () => {
    if (last && document.visibilityState === 'visible') {
      post(`/api/conversations/${id}/read`, { upTo: last }).then(onChange).catch(() => {});
    }
  };

  const loaded = await get(`/api/conversations/${id}/messages`);
  first = loaded.messages[0]?.id ?? 0;
  older.classList.toggle('hidden', !loaded.more);
  scroller.append(older);
  append(loaded.messages);
  markRead();

  older.addEventListener('click', async () => {
    const before = await get(`/api/conversations/${id}/messages?before=${first}`);
    const height = scroller.scrollHeight;
    lastSender = null;
    const els = before.messages.map((m) => messageEl(m));
    older.after(...els);
    first = before.messages[0]?.id ?? first;
    older.classList.toggle('hidden', !before.more);
    scroller.scrollTop += scroller.scrollHeight - height;
  });

  const send = async () => {
    const text = box.value.trim();
    if (!text) {
      return;
    }
    sendButton.disabled = true;
    try {
      await post(`/api/conversations/${id}/messages`, { body: text });
      box.value = '';
      box.style.height = '';
      await poll();
      onChange();
    } catch (err) {
      if (err.code === 'not_taking') {
        setCannot(err.message);
      }
      showError(err);
    } finally {
      sendButton.disabled = false;
      box.focus();
    }
  };
  composer.addEventListener('submit', (e) => {
    e.preventDefault();
    send();
  });
  // Enter sends; Shift+Enter starts a new line.
  box.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
      e.preventDefault();
      send();
    }
  });
  box.addEventListener('input', () => {
    box.style.height = 'auto';
    box.style.height = `${Math.min(box.scrollHeight, 200)}px`;
  });

  const poll = async () => {
    const { messages } = await get(`/api/conversations/${id}/messages?after=${last}`);
    if (messages.length) {
      append(messages);
      markRead();
    }
  };
  const timer = setInterval(() => {
    if (!thread.isConnected) {
      clearInterval(timer);
      return;
    }
    if (document.visibilityState === 'visible') {
      poll().catch(() => {});
    }
  }, THREAD_EVERY_MS);

  const menu = () => [
    { label: conversation.muted ? 'Unmute' : 'Mute', icon: 'bell', onclick: async () => { await patch(`/api/conversations/${id}`, { muted: !conversation.muted }).catch(showError); rerender(); } },
    conversation.kind === 'group' ? { label: 'Open the group', icon: 'users', onclick: () => navigate(`/groups/${conversation.group.id}`) } : null,
    conversation.kind === 'dm' ? {
      label: 'Hide this conversation', icon: 'x', onclick: async () => {
        await patch(`/api/conversations/${id}`, { hidden: true }).catch(showError);
        navigate('/messages');
      },
    } : null,
    conversation.kind === 'dm' && conversation.other ? (conversation.blocked
      ? { label: `Unblock ${conversation.other.name}`, icon: 'user', onclick: async () => { await del(`/api/me/blocks/${conversation.other.id}`).catch(showError); rerender(); } }
      : {
        label: `Block ${conversation.other.name}`, icon: 'lock', onclick: async () => {
          if (await confirmDialog({
            title: `Block ${conversation.other.name}?`,
            message: 'Neither of you can message the other, you won\'t see their messages in group chats, and they can\'t notify you. They aren\'t told.',
            confirm: 'Block', danger: true,
          })) {
            await put(`/api/me/blocks/${conversation.other.id}`).catch(showError);
            rerender();
          }
        },
      }) : null,
  ].filter(Boolean);

  clear(thread,
    h('header', { class: 'thread-head' },
      h('a', { class: 'btn ghost icon-only thread-back', href: '/messages', 'aria-label': 'All conversations' }, icon('menu')),
      picture(conversation),
      h('div', { class: 'grow' }, h('div', { class: 'convo-title' }, conversation.title),
        h('div', { class: 'muted small' }, conversation.kind === 'dm' ? (conversation.other ? `@${conversation.other.handle}` : '') : `${conversation.members} members`)),
      h('button', { type: 'button', class: 'btn ghost icon-only', 'aria-label': 'Conversation menu', onclick: (e) => popup(e.currentTarget, menu()) }, icon('more'))),
    scroller,
    blocked,
    composer);
  scroller.scrollTop = scroller.scrollHeight;
  box.focus();
}

function reportItem(m) {
  return {
    label: 'Report…', icon: 'shield', onclick: async () => {
      const reason = await promptDialog({ title: 'Report this message', label: 'What is wrong with it? (the admins see it with the message)', submit: 'Report' });
      if (reason !== null) {
        await post(`/api/messages/${m.id}/report`, { reason }).catch(showError);
        toast('Reported. Thank you.');
      }
    },
  };
}

// ── Notifications ─────────────────────────────────────────────────────────

export async function renderNotifications(target) {
  const [{ notifications }, settings] = await Promise.all([get('/api/notifications?limit=200'), get('/api/me/notify')]);
  const rows = notifications.map((n) => h('div', { class: `notice${n.read ? '' : ' unread'}` },
    n.actor ? avatar(n.actor) : h('span', { class: 'avatar' }, icon('bell')),
    h('a', { class: 'grow notice-text', href: n.link || '/' }, n.text, h('div', { class: 'muted small' }, timeAgo(n.createdAt))),
    h('button', {
      type: 'button', class: 'btn ghost icon-only', 'aria-label': 'Clear', title: 'Clear', onclick: async (e) => {
        await del(`/api/notifications/${n.id}`).catch(showError);
        e.currentTarget.closest('.notice').remove();
      },
    }, icon('x'))));
  const table = h('table', { class: 'table prefs' },
    h('thead', {}, h('tr', {}, h('th', {}, 'Tell me when'), h('th', {}, 'In Phoenix Cloud'), h('th', {}, 'By email'))),
    h('tbody', {}, settings.kinds.map((k) => {
      const box = (key) => {
        const input = h('input', { type: 'checkbox', checked: k[key], disabled: k.always || (key === 'email' && !settings.hasEmail), 'aria-label': `${k.label}: ${key === 'email' ? 'by email' : 'in Phoenix Cloud'}` });
        input.addEventListener('change', async () => {
          try {
            await patch('/api/me/notify', { [k.kind]: { [key]: input.checked } });
          } catch (err) {
            input.checked = !input.checked;
            showError(err);
          }
        });
        return input;
      };
      return h('tr', {}, h('td', {}, k.label, k.always ? h('div', { class: 'muted small' }, 'Always') : null), h('td', {}, box('inApp')), h('td', {}, box('email')));
    })));
  clear(target,
    h('div', { class: 'page-head' }, h('h1', {}, 'Notifications'), h('div', { class: 'grow' }),
      notifications.some((n) => !n.read) ? h('button', { type: 'button', class: 'btn', onclick: async () => { await post('/api/notifications/read').catch(showError); rerender(); } }, 'Mark all read') : null,
      notifications.length ? h('button', {
        type: 'button', class: 'btn', onclick: async () => {
          if (await confirmDialog({ title: 'Clear all notifications?', message: 'They are deleted, read or not.', confirm: 'Clear all' })) {
            await del('/api/notifications').catch(showError);
            rerender();
          }
        },
      }, 'Clear all') : null),
    h('div', { class: 'stack' },
      notifications.length ? h('div', { class: 'panel notices' }, rows) : h('div', { class: 'empty' }, icon('bell'), h('h2', {}, 'Nothing here'), h('p', {}, 'What concerns you shows up here.')),
      h('div', { class: 'panel stack cards-form', id: 'settings' },
        h('h2', {}, 'What to tell you'),
        settings.hasEmail ? null : h('p', { class: 'muted small flush' }, 'Add an email address on your Account page to get them by email.'),
        h('div', { class: 'table-wrap' }, table))));
  if (notifications.some((n) => !n.read)) {
    post('/api/notifications/read').catch(() => {});
  }
  if (location.hash === '#settings') {
    document.getElementById('settings')?.scrollIntoView();
  }
}

// ── Account settings: who may message you ─────────────────────────────────

const POLICIES = [
  ['everyone', 'Anyone with an account'],
  ['groups', 'People in a group with me'],
  ['team', 'People in my team'],
  ['chosen', 'Only the people I choose below'],
  ['none', 'No one'],
];

export async function messagingPanel() {
  const panel = h('div', { class: 'panel stack cards-form', id: 'messages' });
  const draw = (m) => {
    const policy = h('select', { 'aria-label': 'Who may message me' }, POLICIES.map(([v, l]) => h('option', { value: v, selected: v === m.policy }, l)));
    policy.addEventListener('change', async () => {
      try {
        draw(await patch('/api/me/messaging', { policy: policy.value }));
        toast('Saved.');
      } catch (err) {
        showError(err);
      }
    });
    const peopleList = (people, path, empty, verb) => h('div', { class: 'stack' },
      people.length ? people.map((p) => h('div', { class: 'row' }, h('div', { class: 'grow' }, person(p)),
        h('button', { type: 'button', class: 'btn ghost', onclick: async () => draw(await del(`/api/me/${path}/${p.id}`).catch(showError) ?? m) }, verb)))
        : h('div', { class: 'muted small' }, empty));
    const adder = (path, label, button) => {
      const who = h('input', { type: 'text', placeholder: 'Name or email', 'aria-label': label });
      const form = h('form', { class: 'row' }, who, h('button', { type: 'submit', class: 'btn' }, button));
      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        try {
          draw(await put(`/api/me/${path}/${encodeURIComponent(who.value.trim())}`));
        } catch (err) {
          showError(err);
        }
      });
      return form;
    };
    clear(panel,
      h('h2', {}, 'Messages'),
      h('label', { class: 'field' }, h('span', {}, 'Who can start a direct message with me'), policy,
        h('span', { class: 'muted small' }, 'Anyone you have written to can answer. Group chats are for each group\'s members.')),
      m.policy === 'none' ? null : [h('h3', {}, 'People who can always message me'), peopleList(m.allowed, 'allowed', 'No one chosen.', 'Remove'), adder('allowed', 'Add someone', 'Add')],
      h('h3', {}, 'Blocked'),
      h('p', { class: 'muted small flush' }, 'Blocked people can\'t message you or notify you, and their messages are hidden from you in group chats. They aren\'t told.'),
      peopleList(m.blocked, 'blocks', 'No one blocked.', 'Unblock'),
      adder('blocks', 'Block someone', 'Block'));
  };
  draw(await get('/api/me/messaging'));
  return panel;
}

// ── Admin: reports ────────────────────────────────────────────────────────

export async function adminReports(change) {
  const all = new URLSearchParams(location.search).get('all') === '1';
  const [{ reports }, { reports: documentReports }] = await Promise.all([
    get(`/api/admin/reports${all ? '?all=1' : ''}`), get(`/api/admin/document-reports${all ? '?all=1' : ''}`)]);
  const documentReport = (r) => h('div', { class: 'panel stack' },
    h('div', { class: 'row wrap' },
      h('div', { class: 'grow' }, `${r.reporter?.name ?? 'Someone'} reported a document · ${timeAgo(r.createdAt)}`),
      r.resolvedAt ? h('span', { class: 'badge' }, r.outcome) : [
        h('a', { class: 'btn', href: `/d/${r.document.id}` }, 'Look at it'),
        h('button', { type: 'button', class: 'btn', onclick: change(() => patch(`/api/admin/document-reports/${r.id}`, { outcome: 'dismissed' })) }, 'Dismiss'),
        h('button', { type: 'button', class: 'btn', onclick: change(() => patch(`/api/admin/document-reports/${r.id}`, { outcome: 'flagged', note: r.reason })) }, 'Flag'),
        h('button', { type: 'button', class: 'btn danger', onclick: change(() => patch(`/api/admin/document-reports/${r.id}`, { outcome: 'removed', note: r.reason })) }, 'Remove'),
      ]),
    r.reason ? h('blockquote', { class: 'request' }, r.reason) : null,
    h('a', { class: 'row', href: `/d/${r.document.id}` },
      r.document.thumbnailUrl ? h('img', { class: 'report-thumb', src: r.document.thumbnailUrl, alt: '' }) : null,
      h('div', {}, h('strong', {}, r.document.name), h('div', { class: 'muted small' }, `owned by ${r.document.owner.name ?? 'someone'}${r.document.moderation ? ` · ${r.document.moderation.state}` : ''}`))));
  const quote = (m) => h('div', { class: 'msg-quote' }, h('strong', {}, m.sender?.name ?? 'Deleted account'), ' ',
    h('span', { class: 'muted small' }, timeAgo(m.createdAt)), h('div', {}, m.deleted ? h('em', { class: 'muted' }, 'deleted') : m.body));
  return [
    h('div', { class: 'row' }, h('p', { class: 'muted grow flush' }, 'Messages people reported. Remove one to delete it for everyone; suspend its sender from Accounts if need be.'),
      h('a', { class: 'btn ghost', href: all ? '/admin/reports' : '/admin/reports?all=1' }, all ? 'Open ones only' : 'Show settled ones too')),
    documentReports.length ? [h('h2', {}, 'Documents'), documentReports.map(documentReport)] : null,
    reports.length || documentReports.length ? h('h2', {}, 'Messages') : null,
    reports.length ? reports.map((r) => h('div', { class: 'panel stack' },
      h('div', { class: 'row' }, h('div', { class: 'grow' }, `${r.reporter?.name ?? 'Someone'} reported a ${r.message.kind === 'dm' ? 'direct message' : 'group chat message'} · ${timeAgo(r.createdAt)}`),
        r.resolvedAt ? h('span', { class: 'badge' }, r.outcome) : [
          h('button', { type: 'button', class: 'btn', onclick: change(() => patch(`/api/admin/reports/${r.id}`, { outcome: 'dismissed' })) }, 'Dismiss'),
          h('button', { type: 'button', class: 'btn danger', onclick: change(() => patch(`/api/admin/reports/${r.id}`, { outcome: 'removed' })) }, 'Remove message'),
        ]),
      r.reason ? h('blockquote', { class: 'request' }, r.reason) : null,
      r.context.length ? h('div', { class: 'muted small' }, 'Before it:') : null,
      r.context.map(quote),
      h('div', { class: 'msg-reported' }, quote(r.message))))
      : (documentReports.length ? h('div', { class: 'muted small' }, 'No message reports.') : h('div', { class: 'empty' }, icon('shield'), h('h2', {}, 'No reports'))),
  ];
}
