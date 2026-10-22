// Pages other than the file views: signing in, invites, the account page,
// a group's members and settings, and the admin pages.

import { del, get, patch, post } from './api.js';
import { navigate, refreshMe, reloadShell, rerender, state } from './app.js';
import { renderListing } from './files.js';
import {
  avatar, bytes, clear, confirmDialog, copyText, dateTime, formDialog, h, icon, person, popup, showError, timeAgo, toast,
} from './ui.js';

const LOGIN_ERRORS = {
  beta: 'Phoenix Cloud is in private beta. You need an invite to join.',
  closed: 'Phoenix Cloud is not taking new members right now.',
  invite_invalid: 'That invite has been used, has run out or was turned off.',
  invite_email: 'That invite is for another email address. Sign in with the account it was sent to.',
  email_in_use: 'Someone already signs in with that email. Sign in the way you did before, then link this one from your account page.',
  identity_taken: 'That sign-in belongs to another Phoenix Cloud account.',
  suspended: 'This account is suspended.',
  state: 'That sign-in took too long or was already used. Try again.',
  cancelled: 'Signing in was cancelled.',
  failed: 'Signing in did not work. Try again.',
};

function providerButtons(providers, query) {
  return providers.map((p) => h('a', { class: 'btn wide', href: `/api/auth/start/${p.id}?${query}` }, `Continue with ${p.name}`));
}

function devLogin(returnTo) {
  const handle = h('input', { type: 'text', name: 'handle', placeholder: 'any handle, e.g. alice', 'aria-label': 'Handle', required: true, autofocus: true });
  const go = async (name) => {
    try {
      await post('/api/auth/dev', { handle: name });
      await refreshMe();
      navigate(returnTo, { replace: true });
    } catch (err) {
      showError(err);
    }
  };
  const form = h('form', { class: 'stack' },
    h('div', { class: 'divider' }, 'Dev login (this PC only)'),
    h('div', { class: 'row' }, handle, h('button', { type: 'submit', class: 'btn primary' }, 'Sign in')),
    h('div', { class: 'quick' }, ['alice', 'bob', 'carol'].map((n) => h('button', { type: 'button', class: 'btn ghost', onclick: () => go(n) }, `@${n}`))));
  form.addEventListener('submit', (e) => {
    e.preventDefault();
    go(handle.value.trim());
  });
  return form;
}

export function renderSignin(params) {
  const me = state.me;
  const returnTo = params.get('return') || '/';
  const error = params.get('error');
  const invite = params.get('invite');
  const query = new URLSearchParams({ return: returnTo });
  if (invite) {
    query.set('invite', invite);
  }
  return h('div', { class: 'center' }, h('div', { class: 'signin' },
    h('img', { class: 'logo', src: '/ui/logo.png', alt: '' }),
    h('h1', {}, 'Phoenix Cloud'),
    h('p', {}, me.settings.beta ? 'Private beta. Sign in to see your documents.' : 'Sign in to see your documents.'),
    error ? h('div', { class: 'error' }, LOGIN_ERRORS[error] ?? LOGIN_ERRORS.failed) : null,
    providerButtons(me.providers, query),
    !me.providers.length && !me.devLogin ? h('p', { class: 'small' }, 'No sign-in is set up on this server yet.') : null,
    me.devLogin ? devLogin(returnTo) : null));
}

export function renderInvite(token) {
  const box = h('div', { class: 'signin' }, h('img', { class: 'logo', src: '/ui/logo.png', alt: '' }), h('p', {}, 'Checking your invite…'));
  get(`/api/invites/${encodeURIComponent(token)}`).then((invite) => {
    const me = state.me;
    if (!invite.valid) {
      clear(box, h('img', { class: 'logo', src: '/ui/logo.png', alt: '' }), h('h1', {}, 'This invite is not open'),
        h('p', {}, LOGIN_ERRORS.invite_invalid), h('a', { class: 'btn wide', href: '/signin' }, 'Sign in'));
      return;
    }
    if (me.user) {
      clear(box, h('img', { class: 'logo', src: '/ui/logo.png', alt: '' }), h('h1', {}, 'You are already in'),
        h('p', {}, `You are signed in as @${me.user.handle}. The invite is for someone new.`), h('a', { class: 'btn primary wide', href: '/' }, 'Go to your files'));
      return;
    }
    clear(box,
      h('img', { class: 'logo', src: '/ui/logo.png', alt: '' }),
      h('h1', {}, 'You are invited'),
      h('p', {}, 'Join the Phoenix Cloud beta: keep your Phoenix documents online, open them anywhere and share them.'),
      invite.email ? h('p', { class: 'small' }, `Sign in with the account for ${invite.email}.`) : null,
      providerButtons(me.providers, new URLSearchParams({ invite: token, return: '/' })),
      !me.providers.length ? h('p', { class: 'small' }, 'No sign-in providers are set up on this server yet.') : null);
  }).catch(showError);
  return h('div', { class: 'center' }, box);
}

// ── Account ───────────────────────────────────────────────────────────────

export async function renderAccount(target) {
  const [{ sessions }, { identities }] = await Promise.all([get('/api/me/sessions'), get('/api/me/identities')]);
  if (!target.isConnected) {
    return;
  }
  const me = state.me.user;
  const params = new URLSearchParams(location.search);
  const name = h('input', { type: 'text', name: 'name', value: me.name, maxlength: 80, required: true });
  const handle = h('input', { type: 'text', name: 'handle', value: me.handle, maxlength: 30, required: true });
  const bio = h('textarea', { name: 'bio', rows: 3, maxlength: 500, value: me.bio });
  const profile = h('form', { class: 'panel stack cards-form' },
    h('h2', {}, 'Profile'),
    h('div', { class: 'row' }, avatar(me), h('div', { class: 'muted small' }, me.email ?? '')),
    h('label', { class: 'field' }, h('span', {}, 'Name'), name),
    h('label', { class: 'field' }, h('span', {}, 'Handle'), handle),
    h('label', { class: 'field' }, h('span', {}, 'About you'), bio),
    h('div', {}, h('button', { type: 'submit', class: 'btn primary' }, 'Save')));
  profile.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await patch('/api/me', { name: name.value, handle: handle.value.trim(), bio: bio.value });
      await reloadShell();
      toast('Saved.');
    } catch (err) {
      showError(err);
    }
  });

  const providerName = (id) => state.me.providers.find((p) => p.id === id)?.name ?? id;
  const linked = new Set(identities.map((i) => i.provider));
  const signins = h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Ways to sign in'),
    params.get('error') ? h('div', { class: 'status-bad' }, LOGIN_ERRORS[params.get('error')] ?? LOGIN_ERRORS.failed) : null,
    identities.length ? identities.map((i) => h('div', { class: 'row' },
      h('div', { class: 'grow' }, h('div', {}, providerName(i.provider)), h('div', { class: 'muted small' }, `${i.email ?? ''} · last used ${timeAgo(i.lastUsedAt)}`)),
      h('button', {
        type: 'button', class: 'btn ghost', disabled: identities.length < 2, title: identities.length < 2 ? 'Link another way first' : null, onclick: async () => {
          try {
            await del(`/api/me/identities/${encodeURIComponent(i.provider)}/${encodeURIComponent(i.subject)}`);
            rerender();
          } catch (err) {
            showError(err);
          }
        },
      }, 'Remove'))) : h('div', { class: 'muted' }, 'You sign in with dev login only.'),
    state.me.providers.filter((p) => !linked.has(p.id)).map((p) => h('div', {}, h('a', { class: 'btn', href: `/api/auth/start/${p.id}?link=1` }, `Link ${p.name}`))));

  const sessionList = h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Where you are signed in'),
    sessions.map((s) => h('div', { class: 'row' },
      h('div', { class: 'grow' }, h('div', {}, s.userAgent || 'Unknown browser'), h('div', { class: 'muted small' }, `${s.current ? 'This browser · ' : ''}active ${timeAgo(s.lastSeenAt)} · signed in ${dateTime(s.createdAt)}`)),
      s.current ? null : h('button', { type: 'button', class: 'btn ghost', onclick: async () => { await del(`/api/me/sessions/${s.id}`).catch(showError); rerender(); } }, 'Sign out'))));

  clear(target, h('div', { class: 'page-head' }, h('h1', {}, 'Account')), h('div', { class: 'stack' }, profile, signins, sessionList));
}

// ── Groups ────────────────────────────────────────────────────────────────

export async function newGroupDialog() {
  const result = await formDialog({
    title: 'New group',
    submit: 'Create group',
    note: 'You will be its admin. Add members once it is made.',
    fields: [
      h('label', { class: 'field' }, h('span', {}, 'Name'), h('input', { type: 'text', name: 'name', required: true, maxlength: 80, autofocus: true })),
      h('label', { class: 'field' }, h('span', {}, 'Handle (optional)'), h('input', { type: 'text', name: 'handle', maxlength: 30, placeholder: 'made from the name' })),
    ],
    onSubmit: (data) => post('/api/groups', { name: data.name, handle: data.handle || undefined }),
  });
  if (result?.group) {
    await refreshMe();
    reloadShell();
    navigate(`/groups/${result.group.id}`);
  }
}

function groupTabs(group, role, tab) {
  return h('div', { class: 'tabs' },
    h('a', { href: `/groups/${group.id}`, class: tab === 'files' ? 'active' : null }, 'Files'),
    h('a', { href: `/groups/${group.id}/members`, class: tab === 'members' ? 'active' : null }, 'Members'),
    role === 'admin' ? h('a', { href: `/groups/${group.id}/settings`, class: tab === 'settings' ? 'active' : null }, 'Settings') : null);
}

function membersView(detail, rerenderPage) {
  const { group, role, members } = detail;
  const admin = role === 'admin';
  const me = state.me.user;
  const act = (fn) => async (...args) => {
    try {
      await fn(...args);
      rerenderPage();
    } catch (err) {
      showError(err);
    }
  };
  const who = h('input', { type: 'text', placeholder: 'Handle or email', 'aria-label': 'Who', required: true });
  const memberRole = h('select', { 'aria-label': 'Role' }, h('option', { value: 'member' }, 'Member'), h('option', { value: 'admin' }, 'Admin'));
  const add = h('form', { class: 'row cards-form' }, who, memberRole, h('button', { type: 'submit', class: 'btn primary' }, icon('plus'), 'Add'));
  add.addEventListener('submit', (e) => {
    e.preventDefault();
    act(() => post(`/api/groups/${group.id}/members`, { who: who.value.trim(), role: memberRole.value }))();
  });
  const rows = members.map((m) => {
    const roleBox = h('select', { 'aria-label': 'Role', disabled: !admin },
      h('option', { value: 'member', selected: m.role === 'member' }, 'Member'), h('option', { value: 'admin', selected: m.role === 'admin' }, 'Admin'));
    roleBox.addEventListener('change', act(() => patch(`/api/groups/${group.id}/members/${m.user.id}`, { role: roleBox.value })));
    const self = m.user.id === me.id;
    const leave = async () => {
      if (!(await confirmDialog({ title: `Leave ${group.name}?`, message: 'You will lose access to its files unless they are shared with you.', confirm: 'Leave', danger: true }))) {
        return;
      }
      try {
        await del(`/api/groups/${group.id}/members/${m.user.id}`);
        await reloadShell();
        navigate('/');
      } catch (err) {
        showError(err);
      }
    };
    const remove = admin || self ? h('button', {
      type: 'button', class: 'btn ghost', onclick: self ? leave : act(() => del(`/api/groups/${group.id}/members/${m.user.id}`)),
    }, self ? 'Leave' : 'Remove') : null;
    return h('tr', {}, h('td', {}, person(m.user)), h('td', {}, roleBox), h('td', { class: 'muted' }, `added ${timeAgo(m.addedAt)}`), h('td', {}, remove));
  });
  return [
    admin ? add : null,
    admin && state.me.settings.beta ? h('p', { class: 'muted small' }, 'During the beta, only people who already have an account can be added.') : null,
    h('div', { class: 'table-wrap' }, h('table', { class: 'table' },
      h('thead', {}, h('tr', {}, h('th', {}, 'Member'), h('th', {}, 'Role'), h('th', {}, 'Joined'), h('th'))),
      h('tbody', {}, rows))),
    h('p', { class: 'muted small' }, 'Admins add and remove members, choose what members can do with the group\'s files, and own those files. A group keeps at least one admin.'),
  ];
}

function settingsView(detail) {
  const { group } = detail;
  const name = h('input', { type: 'text', value: group.name, maxlength: 80, required: true });
  const handle = h('input', { type: 'text', value: group.handle, maxlength: 30, required: true });
  const description = h('textarea', { rows: 3, maxlength: 500, value: group.description });
  const defaultRole = h('select', {},
    [['viewer', 'View them'], ['commenter', 'Comment on them'], ['editor', 'Edit them']].map(([v, l]) => h('option', { value: v, selected: group.defaultRole === v }, l)));
  const form = h('form', { class: 'panel stack cards-form' },
    h('h2', {}, 'Group'),
    h('label', { class: 'field' }, h('span', {}, 'Name'), name),
    h('label', { class: 'field' }, h('span', {}, 'Handle'), handle),
    h('label', { class: 'field' }, h('span', {}, 'Description'), description),
    h('label', { class: 'field' }, h('span', {}, 'Members can… the group\'s files'), defaultRole),
    h('div', {}, h('button', { type: 'submit', class: 'btn primary' }, 'Save')));
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await patch(`/api/groups/${group.id}`, { name: name.value, handle: handle.value.trim(), description: description.value, defaultRole: defaultRole.value });
      await reloadShell();
      toast('Saved.');
    } catch (err) {
      showError(err);
    }
  });
  const danger = h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Delete the group'),
    h('p', { class: 'muted flush' }, 'Its files become yours, in a folder named after it. Members lose access to them.'),
    h('div', {}, h('button', {
      type: 'button', class: 'btn danger', onclick: async () => {
        if (!(await confirmDialog({ title: `Delete ${group.name}?`, message: 'This cannot be undone. Its files move to your My Files.', confirm: 'Delete group', danger: true }))) {
          return;
        }
        try {
          await del(`/api/groups/${group.id}`);
          await reloadShell();
          navigate('/files');
        } catch (err) {
          showError(err);
        }
      },
    }, icon('trash'), 'Delete group')));
  return [form, danger];
}

export async function renderGroupPage(target, id, tab, folder) {
  const detail = await get(`/api/groups/${id}`);
  if (!target.isConnected) {
    return;
  }
  const { group, role } = detail;
  const head = h('div', { class: 'page-head' }, h('span', { class: 'avatar' }, icon('users')), h('div', { class: 'grow' },
    h('h1', {}, group.name), h('div', { class: 'muted small' }, `@${group.handle} · ${detail.members.length} member${detail.members.length === 1 ? '' : 's'}${role === 'admin' ? ' · you are an admin' : ''}`)));
  const before = h('div', {}, head, group.description ? h('p', { class: 'muted' }, group.description) : null, groupTabs(group, role, tab));
  if (tab === 'files') {
    await renderListing(target, {
      view: 'group', groupId: id, folder, title: 'Files', before,
      rootHref: `/groups/${id}`, folderHref: (f) => `/groups/${id}/f/${f}`,
    });
    return;
  }
  if (tab === 'settings' && role === 'admin') {
    clear(target, before, settingsView(detail));
    return;
  }
  clear(target, before, membersView(detail, rerender));
}

// ── Admin ─────────────────────────────────────────────────────────────────

function count(n, one, many = `${one}s`) {
  return `${n} ${n === 1 ? one : many}`;
}

function adminTabs(tab) {
  return h('div', { class: 'tabs' }, [['users', 'People'], ['invites', 'Invites'], ['settings', 'Settings'], ['audit', 'Activity']]
    .map(([t, label]) => h('a', { href: `/admin/${t}`, class: tab === t ? 'active' : null }, label)));
}

async function adminUsers() {
  const [{ users }, stats] = await Promise.all([get('/api/admin/users'), get('/api/admin/stats')]);
  const me = state.me.user;
  const act = (fn) => async () => {
    try {
      await fn();
      rerender();
    } catch (err) {
      showError(err);
    }
  };
  const quota = (u) => async () => {
    const result = await formDialog({
      title: `Storage for @${u.handle}`,
      fields: h('label', { class: 'field' }, h('span', {}, 'Quota in MB (empty for the default)'), h('input', { type: 'number', name: 'quota', min: 0, value: u.quotaMB ?? '' })),
      onSubmit: (data) => patch(`/api/admin/users/${u.id}`, { quotaMB: data.quota === '' ? null : Number(data.quota) }),
    });
    if (result) {
      rerender();
    }
  };
  return [
    h('p', { class: 'muted' }, `${count(stats.users, 'person', 'people')} · ${count(stats.groups, 'group')} · ${count(stats.documents, 'document')} (${stats.trashed} in the trash) · ${count(stats.revisions, 'version')} · ${bytes(stats.blobs.bytes)} stored in ${count(stats.blobs.count, 'chunk')} · database ${bytes(stats.databaseBytes)}`),
    h('div', { class: 'table-wrap' }, h('table', { class: 'table' },
      h('thead', {}, h('tr', {}, ['Person', 'Email', 'Signs in with', 'Files', 'Storage', 'Last seen', 'Status', ''].map((t) => h('th', {}, t)))),
      h('tbody', {}, users.map((u) => h('tr', {},
        h('td', {}, person(u, u.isAdmin ? ' · admin' : '')),
        h('td', {}, u.email ?? ''),
        h('td', {}, u.providers.join(', ') || 'dev login'),
        h('td', {}, String(u.documents)),
        h('td', {}, `${bytes(u.bytes)}${u.quotaMB !== null ? ` of ${u.quotaMB} MB` : ''}`),
        h('td', {}, timeAgo(u.lastSeenAt)),
        h('td', { class: u.status === 'active' ? 'status-ok' : 'status-bad' }, u.status),
        h('td', {}, u.id === me.id ? null : h('button', {
          type: 'button', class: 'btn ghost icon-only', 'aria-label': 'Actions', onclick: (e) => popup(e.currentTarget, [
            { label: u.status === 'active' ? 'Suspend' : 'Unsuspend', icon: 'lock', danger: u.status === 'active', onclick: act(() => patch(`/api/admin/users/${u.id}`, { status: u.status === 'active' ? 'suspended' : 'active' })) },
            { label: u.isAdmin ? 'Remove admin' : 'Make admin', icon: 'shield', onclick: act(() => patch(`/api/admin/users/${u.id}`, { isAdmin: !u.isAdmin })) },
            { label: 'Storage quota…', icon: 'settings', onclick: quota(u) },
          ]),
        }, icon('more')))))))),
  ];
}

async function adminInvites() {
  const { invites } = await get('/api/admin/invites');
  const fresh = h('div');
  const email = h('input', { type: 'email', placeholder: 'Their email (optional; holds the invite to it)', 'aria-label': 'Email' });
  const note = h('input', { type: 'text', placeholder: 'Note, e.g. who it is for', 'aria-label': 'Note', maxlength: 200 });
  const days = h('select', { 'aria-label': 'Lasts' }, [['14', '2 weeks'], ['7', '1 week'], ['30', '30 days'], ['0', 'No limit']].map(([v, l]) => h('option', { value: v }, l)));
  const admin = h('input', { type: 'checkbox' });
  const form = h('form', { class: 'panel stack cards-form' },
    h('h2', {}, 'Invite someone'),
    email, note,
    h('div', { class: 'row' }, h('span', { class: 'muted small' }, 'Lasts'), days, h('label', { class: 'check' }, admin, 'Make them an admin')),
    h('div', {}, h('button', { type: 'submit', class: 'btn primary' }, icon('link'), 'Make invite link')),
    fresh);
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      const made = await post('/api/admin/invites', { email: email.value.trim() || null, note: note.value, days: Number(days.value), makeAdmin: admin.checked });
      clear(fresh,
        h('div', { class: 'copy-row' }, h('input', { type: 'text', value: made.url, readOnly: true, onfocus: (ev) => ev.target.select() }),
          h('button', { type: 'button', class: 'btn', onclick: () => copyText(made.url) }, icon('copy'), 'Copy')),
        h('p', { class: 'muted small' }, 'Send this link yourself. It is shown only once and works for one person.'));
      email.value = '';
      note.value = '';
      const list = await adminInviteTable();
      form.parentNode.querySelector('.invite-table')?.replaceWith(list);
    } catch (err) {
      showError(err);
    }
  });
  return [form, h('div', { class: 'section-title' }, 'Invites'), inviteTable(invites)];
}

function inviteTable(invites) {
  return h('div', { class: 'table-wrap invite-table' }, h('table', { class: 'table' },
    h('thead', {}, h('tr', {}, ['For', 'Note', 'Made', 'Status', ''].map((t) => h('th', {}, t)))),
    h('tbody', {}, invites.map((i) => h('tr', {},
      h('td', {}, i.email ?? 'anyone with the link', i.makeAdmin ? h('span', { class: 'muted' }, ' · admin') : null),
      h('td', {}, i.note),
      h('td', {}, `${timeAgo(i.createdAt)}${i.createdBy ? ` by ${i.createdBy.name}` : ''}`),
      h('td', { class: i.status === 'pending' ? '' : 'muted' }, i.status === 'claimed' && i.claimedBy ? `claimed by @${i.claimedBy.handle}` : i.status),
      h('td', {}, i.status === 'pending' ? h('button', {
        type: 'button', class: 'btn ghost', onclick: async () => {
          await del(`/api/admin/invites/${i.id}`).catch(showError);
          rerender();
        },
      }, 'Turn off') : null))))));
}

async function adminInviteTable() {
  return inviteTable((await get('/api/admin/invites')).invites);
}

async function adminSettings() {
  const { settings } = await get('/api/admin/settings');
  const policy = h('select', {}, [['invite_only', 'By invite only (closed beta)'], ['open', 'Open to anyone'], ['closed', 'Closed to new people']]
    .map(([v, l]) => h('option', { value: v, selected: settings.signupPolicy === v }, l)));
  const beta = h('input', { type: 'checkbox', checked: settings.beta });
  const quota = h('input', { type: 'number', min: 1, value: settings.defaultQuotaMB });
  const banner = h('input', { type: 'text', value: settings.banner, maxlength: 300, placeholder: 'A message at the top of every page' });
  const form = h('form', { class: 'panel stack cards-form' },
    h('label', { class: 'field' }, h('span', {}, 'Who can join'), policy),
    h('label', { class: 'check' }, beta, 'Beta: public and link-shared documents only for signed-in members; ask search engines not to index'),
    h('label', { class: 'field' }, h('span', {}, 'Storage for each person and group, in MB'), quota),
    h('label', { class: 'field' }, h('span', {}, 'Banner'), banner),
    h('div', {}, h('button', { type: 'submit', class: 'btn primary' }, 'Save')));
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await patch('/api/admin/settings', { signupPolicy: policy.value, beta: beta.checked, defaultQuotaMB: Number(quota.value), banner: banner.value });
      await reloadShell();
      toast('Saved.');
    } catch (err) {
      showError(err);
    }
  });
  return [form];
}

async function adminAudit() {
  const { entries } = await get('/api/admin/audit');
  return [h('div', { class: 'table-wrap' }, h('table', { class: 'table' },
    h('thead', {}, h('tr', {}, ['When', 'Who', 'What', 'On', 'Details'].map((t) => h('th', {}, t)))),
    h('tbody', {}, entries.map((e) => h('tr', {},
      h('td', { title: dateTime(e.ts) }, timeAgo(e.ts)),
      h('td', {}, e.actor ? `@${e.actor.handle}` : 'system'),
      h('td', {}, e.action),
      h('td', {}, e.targetKind === 'document' ? h('a', { href: `/d/${e.targetId}` }, 'document') : `${e.targetKind} ${e.targetId}`),
      h('td', { class: 'muted small' }, Object.keys(e.detail).length ? JSON.stringify(e.detail) : ''))))))];
}

export async function renderAdmin(target, tab) {
  if (!state.me.user.isAdmin) {
    clear(target, h('div', { class: 'empty' }, icon('lock'), h('h2', {}, 'Admins only')));
    return;
  }
  const build = { users: adminUsers, invites: adminInvites, settings: adminSettings, audit: adminAudit }[tab] ?? adminUsers;
  const content = await build();
  if (target.isConnected) {
    clear(target, h('div', { class: 'page-head' }, h('h1', {}, 'Admin')), adminTabs(tab), content);
  }
}
