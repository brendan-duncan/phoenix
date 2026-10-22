// The dashboard: the shell (sidebar, top bar) and the router. Pages render
// themselves into <main> from files.js and pages.js.

import { get, post, setCsrf, setLink } from './api.js';
import { newDocumentDialog, renderDocument, renderListing, UPLOAD_TYPES, uploadFiles } from './files.js';
import {
  newGroupDialog, renderAccount, renderAdmin, renderConfirm, renderForgot, renderGroupPage, renderInvite, renderLocked, renderPlayer, renderRegister, renderReset, renderSignin,
} from './pages.js';
import { renderMessages, renderNotifications } from './messages.js';
import { avatar, clear, closeMenu, h, icon, popup, promptDialog, showError, timeAgo, toast } from './ui.js';

export const state = {
  me: null,
  view: localStorage.getItem('phoenix.cloud.view') === 'list' ? 'list' : 'grid',
};

let shell = null;
let main = null;
let renderId = 0;

export function navigate(path, { replace = false } = {}) {
  if (replace) {
    history.replaceState(null, '', path);
  } else {
    history.pushState(null, '', path);
  }
  route();
}

export async function refreshMe() {
  state.me = await get('/api/me');
  setCsrf(state.me.csrf);
  return state.me;
}

export function setView(view) {
  state.view = view;
  localStorage.setItem('phoenix.cloud.view', view);
  route();
}

/** Theme: the system's, or one chosen in the account menu. */
function applyTheme(theme = localStorage.getItem('phoenix.cloud.theme')) {
  if (theme === 'light' || theme === 'dark') {
    document.documentElement.dataset.theme = theme;
  } else {
    delete document.documentElement.dataset.theme;
  }
}

export function setTheme(theme) {
  if (theme) {
    localStorage.setItem('phoenix.cloud.theme', theme);
  } else {
    localStorage.removeItem('phoenix.cloud.theme');
  }
  applyTheme(theme);
}

/** Where a new upload or folder goes from the current page, and whether files dropped on it upload there. */
export const place = { groupId: null, folderId: null, editable: true, drop: false };

/** Renders the current page again (after a change). */
export function rerender() {
  route();
}

function navLink(href, iconName, label, key) {
  return h('a', { href, dataset: { nav: key } }, icon(iconName), h('span', { class: 'name' }, label));
}

function buildShell() {
  const me = state.me;
  const search = h('input', { type: 'search', placeholder: 'Search your files', 'aria-label': 'Search' });
  search.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      const q = search.value.trim();
      navigate(q ? `/search?q=${encodeURIComponent(q)}` : '/');
    }
  });
  const groups = h('div', { class: 'nav' }, me.groups.map((g) => navLink(`/groups/${g.id}`, 'users', g.name, `group:${g.id}`)));
  const fileInput = h('input', { type: 'file', accept: UPLOAD_TYPES, multiple: true, class: 'hidden' });
  fileInput.addEventListener('change', () => {
    uploadFiles([...fileInput.files]);
    fileInput.value = '';
  });
  const newButton = h('button', {
    type: 'button',
    class: 'btn primary wide new',
    onclick: (e) => popup(e.currentTarget, [
      { label: 'New document', icon: 'file', onclick: () => newDocumentDialog() },
      { label: 'Start a tutorial', icon: 'play', onclick: () => newDocumentDialog('tutorials') },
      { label: 'Upload files', icon: 'upload', onclick: () => fileInput.click() },
      { label: 'New folder', icon: 'folder', onclick: () => newFolder() },
      '-',
      { label: 'New group', icon: 'users', onclick: () => newGroupDialog() },
    ]),
  }, icon('plus'), 'New');

  const messagesCount = h('span', { class: 'count hidden' });
  const messagesLink = navLink('/messages', 'chat', 'Messages', 'messages');
  messagesLink.append(messagesCount);
  shellCounts.messages = (n) => {
    messagesCount.textContent = n > 99 ? '99+' : String(n);
    messagesCount.classList.toggle('hidden', !n);
  };
  shellCounts.messages(me.unreadMessages ?? 0);

  const sidebar = h('nav', { class: 'sidebar', 'aria-label': 'Places' },
    h('a', { class: 'brand', href: '/' }, h('img', { src: '/ui/logo.png', alt: '' }), 'Phoenix Cloud', me.settings.beta ? h('span', { class: 'pill' }, 'Beta') : null),
    newButton,
    fileInput,
    h('div', { class: 'nav' },
      navLink('/', 'clock', 'Recents', 'recent'),
      navLink('/files', 'folder', 'My Files', 'files'),
      navLink('/shared', 'share', 'Shared with me', 'shared'),
      navLink('/favorites', 'star', 'Favorites', 'favorites'),
      messagesLink),
    h('div', { class: 'nav-head' }, 'Groups',
      h('button', { type: 'button', class: 'btn ghost icon-only', title: 'New group', 'aria-label': 'New group', onclick: () => newGroupDialog() }, icon('plus'))),
    me.groups.length ? groups : h('div', { class: 'muted small nav-empty' }, 'No groups yet'),
    h('div', { class: 'nav bottom' },
      navLink('/trash', 'trash', 'Trash', 'trash'),
      me.user.isAdmin ? navLink('/admin', 'shield', 'Admin', 'admin') : null));

  const account = h('button', {
    type: 'button',
    class: 'avatar-btn',
    'aria-label': 'Account',
    onclick: (e) => popup(e.currentTarget, [
      { head: me.user.name },
      { label: 'Account', icon: 'user', onclick: () => navigate('/account') },
      { label: 'Theme: system', onclick: () => setTheme(null) },
      { label: 'Theme: light', onclick: () => setTheme('light') },
      { label: 'Theme: dark', onclick: () => setTheme('dark') },
      '-',
      { label: 'Sign out', icon: 'logout', onclick: signOut },
    ]),
  }, avatar(me.user));

  const topbar = h('header', { class: 'topbar' },
    h('button', { type: 'button', class: 'btn ghost icon-only menu-toggle', 'aria-label': 'Menu', onclick: () => shell.classList.toggle('drawer') }, icon('menu')),
    h('div', { class: 'search' }, icon('search'), search),
    h('div', { class: 'grow' }),
    bell(),
    account);

  main = h('main', { id: 'main' });
  shell = h('div', { class: 'shell' }, sidebar, topbar, main);
  shell.addEventListener('click', (e) => {
    // Tapping outside the drawer closes it.
    if (shell.classList.contains('drawer') && !sidebar.contains(e.target) && !e.target.closest('.menu-toggle')) {
      shell.classList.remove('drawer');
    }
  });
  shell.search = search;
  return shell;
}

/** The bell: what happened that concerns this person, with how many are unread. */
// Updates the shell's counts (unread messages) as /api/me is asked again.
const shellCounts = { messages: () => {} };

/** Shows `n` unread messages beside Messages in the sidebar. */
export function showUnreadMessages(n) {
  shellCounts.messages(n);
}

function bell() {
  const count = h('span', { class: 'bell-count hidden' });
  const show = (n) => {
    count.textContent = n > 9 ? '9+' : String(n);
    count.classList.toggle('hidden', !n);
  };
  show(state.me.unread ?? 0);
  const button = h('button', { type: 'button', class: 'btn ghost icon-only bell', 'aria-label': 'Notifications', title: 'Notifications' }, icon('bell'), count);
  button.addEventListener('click', async () => {
    let list = [];
    try {
      ({ notifications: list } = await get('/api/notifications'));
    } catch (err) {
      showError(err);
      return;
    }
    popup(button, [
      ...(list.length
        ? [{ head: 'Notifications' }, ...list.slice(0, 15).map((n) => ({
          label: `${n.read ? '' : '● '}${n.text} · ${timeAgo(n.createdAt)}`,
          onclick: () => navigate(n.link || '/'),
        }))]
        : [{ head: 'Nothing new' }]),
      '-',
      { label: 'See all', icon: 'bell', onclick: () => navigate('/notifications') },
      { label: 'What to tell me…', icon: 'settings', onclick: () => navigate('/notifications#settings') },
    ]);
    if (list.some((n) => !n.read)) {
      post('/api/notifications/read').then(({ unread }) => show(unread)).catch(() => {});
    }
  });
  // Checks now and then while the page is open.
  setInterval(() => {
    if (document.visibilityState === 'visible') {
      get('/api/me').then((me) => {
        show(me.unread ?? 0);
        shellCounts.messages(me.unreadMessages ?? 0);
      }).catch(() => {});
    }
  }, 30 * 1000);
  return button;
}

/**
 * Fetches the web editor into the browser's cache while nothing else is
 * going on, so Edit and Play open quickly.
 */
function warmEditor() {
  if (!state.me.editor || warmEditor.done) {
    return;
  }
  warmEditor.done = true;
  const idle = window.requestIdleCallback ?? ((fn) => setTimeout(fn, 3000));
  idle(() => {
    for (const file of ['/editor/qtloader.js', '/editor/Phoenix.js', '/editor/Phoenix.wasm']) {
      document.head.append(h('link', { rel: 'prefetch', href: file }));
    }
  });
}

async function newFolder() {
  if (!place.editable) {
    place.groupId = null;
    place.folderId = null;
  }
  const name = await promptDialog({ title: 'New folder', label: 'Name', value: 'New folder', submit: 'Create' });
  if (!name) {
    return;
  }
  try {
    await post('/api/folders', { name, parentId: place.folderId, groupId: place.groupId });
    toast(`Made “${name}”.`);
    route();
  } catch (err) {
    showError(err);
  }
}

async function signOut() {
  await post('/api/auth/logout');
  shell = null;
  await refreshMe();
  navigate('/signin', { replace: true });
}

function markNav(key) {
  for (const a of shell.querySelectorAll('.nav a')) {
    a.classList.toggle('active', a.dataset.nav === key);
  }
}

/** Rebuilds the shell, as after joining or leaving a group. */
export async function reloadShell() {
  await refreshMe();
  shell = null;
  route();
}

const ROUTES = [
  [/^\/$/, () => ({ nav: 'recent', render: (m) => renderListing(m, { view: 'recent', title: 'Recents' }) })],
  [/^\/files(?:\/([^/]+))?$/, ([folder]) => ({ nav: 'files', render: (m) => renderListing(m, { view: 'files', folder, title: 'My Files' }) })],
  [/^\/shared$/, () => ({ nav: 'shared', render: (m) => renderListing(m, { view: 'shared', title: 'Shared with me' }) })],
  [/^\/favorites$/, () => ({ nav: 'favorites', render: (m) => renderListing(m, { view: 'favorites', title: 'Favorites' }) })],
  [/^\/trash$/, () => ({ nav: 'trash', render: (m) => renderListing(m, { view: 'trash', title: 'Trash' }) })],
  [/^\/search$/, () => ({ nav: null, render: (m) => renderListing(m, { view: 'search', q: new URLSearchParams(location.search).get('q') ?? '', title: 'Search' }) })],
  [/^\/groups\/([^/]+)(?:\/(members|settings)|\/f\/([^/]+))?$/, ([id, tab, folder]) => ({ nav: `group:${id}`, render: (m) => renderGroupPage(m, id, tab ?? 'files', folder) })],
  [/^\/d\/([^/]+)$/, ([id]) => ({ nav: null, render: (m) => renderDocument(m, id) })],
  [/^\/account$/, () => ({ nav: null, render: (m) => renderAccount(m) })],
  [/^\/messages(?:\/([^/]+))?$/, ([id]) => ({ nav: 'messages', render: (m) => renderMessages(m, id) })],
  [/^\/notifications$/, () => ({ nav: null, render: (m) => renderNotifications(m) })],
  [/^\/admin(?:\/(\w+))?$/, ([tab]) => ({ nav: 'admin', render: (m) => renderAdmin(m, tab ?? 'users') })],
];

async function route() {
  closeMenu();
  const root = document.getElementById('app');
  const path = location.pathname;
  const params = new URLSearchParams(location.search);
  // A share link's token rides along on the document page only.
  setLink(path.startsWith('/d/') ? params.get('link') : null);

  const outside = { '/signin': renderSignin, '/register': renderRegister, '/forgot': renderForgot };
  if (outside[path] || path.startsWith('/invite/') || path.startsWith('/confirm/') || path.startsWith('/reset/')) {
    shell = null;
    place.drop = false;
    if (state.me.user && (path === '/signin' || path === '/register')) {
      navigate(params.get('return') || '/', { replace: true });
      return;
    }
    const token = decodeURIComponent(path.slice(path.indexOf('/', 1) + 1));
    const tokenPages = { '/invite/': renderInvite, '/confirm/': renderConfirm, '/reset/': renderReset };
    const tokenPage = Object.entries(tokenPages).find(([prefix]) => path.startsWith(prefix))?.[1];
    clear(root, outside[path] ? outside[path](params) : tokenPage(token));
    return;
  }
  if (!state.me.user) {
    const back = path.startsWith('/d/') || path.startsWith('/play/') ? `${path}${location.search}` : path;
    navigate(`/signin?return=${encodeURIComponent(back)}`, { replace: true });
    return;
  }
  if (state.me.settings.locked && !state.me.user.isAdmin) {
    shell = null;
    place.drop = false;
    clear(root, renderLocked(signOut));
    return;
  }
  const player = /^\/play\/([^/]+)$/.exec(path);
  if (player) {
    shell = null;
    place.drop = false;
    clear(root, renderPlayer(decodeURIComponent(player[1])));
    return;
  }
  if (!shell) {
    clear(root, buildShell());
    warmEditor();
  }
  shell.classList.remove('drawer');
  const found = ROUTES.map(([re, make]) => {
    const m = re.exec(path);
    return m ? make(m.slice(1).map((v) => (v === undefined ? undefined : decodeURIComponent(v)))) : null;
  }).find(Boolean);
  const page = found ?? { nav: null, render: (m) => clear(m, h('div', { class: 'empty' }, h('h2', {}, 'Nothing here'), h('a', { href: '/', class: 'btn' }, 'Go to Recents'))) };
  markNav(page.nav);
  if (path === '/search') {
    shell.search.value = params.get('q') ?? '';
  }
  place.groupId = null;
  place.folderId = null;
  place.editable = true;
  // Files dropped on any page upload: here, or into My Files.
  place.drop = true;
  const id = ++renderId;
  const target = h('div', { class: 'page' });
  clear(main,
    state.me.user.passwordIsDefault
      ? h('div', { class: 'banner' }, 'You are using the password this server started with. ', h('a', { class: 'link', href: '/account#password' }, 'Choose your own password'), '.')
      : null,
    state.me.settings.locked
      ? h('div', { class: 'banner warn' }, 'Phoenix Cloud is locked: only admins can use it. ', h('a', { class: 'link', href: '/admin/settings' }, 'Unlock it'), '.')
      : null,
    state.me.settings.banner ? h('div', { class: 'banner' }, state.me.settings.banner) : null,
    target);
  try {
    await page.render(target);
  } catch (err) {
    if (id !== renderId) {
      return;
    }
    if (err.status === 401) {
      await refreshMe();
      shell = null;
      route();
      return;
    }
    clear(target, h('div', { class: 'empty' }, icon('lock'), h('h2', {}, err.status === 404 ? 'Not found' : 'Something went wrong'), h('p', {}, err.message)));
  }
}

document.addEventListener('click', (e) => {
  const a = e.target.closest('a[href]');
  if (!a || a.target || e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) {
    return;
  }
  const url = new URL(a.href, location.href);
  if (url.origin !== location.origin || url.pathname.startsWith('/api/') || url.pathname.startsWith('/editor/') || a.hasAttribute('download')) {
    return;
  }
  e.preventDefault();
  navigate(url.pathname + url.search);
});

addEventListener('popstate', route);

applyTheme();
try {
  await refreshMe();
  document.getElementById('app').classList.remove('booting');
  document.getElementById('app').removeAttribute('aria-busy');
  route();
} catch (err) {
  clear(document.getElementById('app'), h('div', { class: 'center' }, h('div', { class: 'signin' }, h('h1', {}, 'Phoenix Cloud is not answering'), h('p', {}, err.message))));
  document.getElementById('app').classList.remove('booting');
}
