// Building the page: elements from data (never HTML from data), icons,
// dialogs, pop-up menus, toasts, and formatting.

/**
 * h('div', { class: 'x', onclick: fn }, child, 'text', [more]) makes an
 * element. Children may be nodes, strings (as text) or arrays; null and
 * false are skipped.
 */
export function h(tag, attrs = {}, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs ?? {})) {
    if (value === null || value === undefined || value === false) {
      continue;
    }
    if (key.startsWith('on') && typeof value === 'function') {
      el.addEventListener(key.slice(2), value);
    } else if (key === 'class') {
      el.className = value;
    } else if (key === 'dataset') {
      Object.assign(el.dataset, value);
    } else if (key === 'value') {
      el.value = value;
    } else if (key === 'checked' || key === 'selected' || key === 'disabled' || key === 'readOnly') {
      el[key] = Boolean(value);
    } else {
      el.setAttribute(key, value === true ? '' : String(value));
    }
  }
  append(el, children);
  return el;
}

function append(el, children) {
  for (const child of children) {
    if (child === null || child === undefined || child === false) {
      continue;
    }
    if (Array.isArray(child)) {
      append(el, child);
    } else {
      el.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
  }
}

export function clear(el, ...children) {
  el.replaceChildren();
  append(el, children);
  return el;
}

// Icons: constant SVG markup only, on a 24-unit grid with round strokes.
const ICONS = {
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  users: '<circle cx="9" cy="8" r="3.5"/><path d="M2.5 20c0-3.6 2.9-6 6.5-6s6.5 2.4 6.5 6"/><circle cx="17" cy="9" r="2.5"/><path d="M17 14c2.5 0 4.5 1.8 4.5 4.5"/>',
  share: '<circle cx="18" cy="5" r="2.5"/><circle cx="6" cy="12" r="2.5"/><circle cx="18" cy="19" r="2.5"/><path d="M8.2 10.8l7.6-4.6M8.2 13.2l7.6 4.6"/>',
  star: '<path d="M12 3l2.8 5.8 6.2.8-4.5 4.4 1.1 6.3L12 17.3l-5.6 3 1.1-6.3L3 9.6l6.2-.8z"/>',
  trash: '<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  upload: '<path d="M12 16V4M7 9l5-5 5 5M4 20h16"/>',
  download: '<path d="M12 4v12M7 11l5 5 5-5M4 20h16"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4"/>',
  grid: '<rect x="4" y="4" width="7" height="7" rx="1.5"/><rect x="13" y="4" width="7" height="7" rx="1.5"/><rect x="4" y="13" width="7" height="7" rx="1.5"/><rect x="13" y="13" width="7" height="7" rx="1.5"/>',
  list: '<path d="M8 6h13M8 12h13M8 18h13M3.5 6h.01M3.5 12h.01M3.5 18h.01"/>',
  more: '<circle cx="5" cy="12" r="1.3" fill="currentColor"/><circle cx="12" cy="12" r="1.3" fill="currentColor"/><circle cx="19" cy="12" r="1.3" fill="currentColor"/>',
  shield: '<path d="M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6z"/>',
  logout: '<path d="M15 4h4a1 1 0 0 1 1 1v14a1 1 0 0 1-1 1h-4M10 17l5-5-5-5M15 12H3"/>',
  link: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>',
  x: '<path d="M6 6l12 12M18 6L6 18"/>',
  edit: '<path d="M4 20h4L19 9l-4-4L4 16z"/>',
  copy: '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2"/>',
  history: '<path d="M3 12a9 9 0 1 0 3-6.7L3 8M3 3v5h5M12 7v5l3 2"/>',
  menu: '<path d="M4 6h16M4 12h16M4 18h16"/>',
  file: '<path d="M6 3h8l5 5v13H6zM14 3v5h5"/>',
  user: '<circle cx="12" cy="8" r="4"/><path d="M4 21c0-4 3.6-7 8-7s8 3 8 7"/>',
  globe: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c2.5 2.7 2.5 15.3 0 18M12 3c-2.5 2.7-2.5 15.3 0 18"/>',
  lock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
  restore: '<path d="M3 12a9 9 0 1 0 3-6.7L3 8M3 3v5h5"/>',
  settings: '<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/>',
  sparkle: '<path d="M12 3l1.8 5.4L19 10l-5.2 1.6L12 17l-1.8-5.4L5 10l5.2-1.6z"/>',
  move: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2zM10 13h6M13 10l3 3-3 3"/>',
  play: '<path d="M7 4l13 8-13 8z"/>',
  chat: '<path d="M4 5h16v11H9l-5 4z"/>',
  bell: '<path d="M6 9a6 6 0 0 1 12 0c0 6 3 7 3 7H3s3-1 3-7M10 20a2 2 0 0 0 4 0"/>',
};

export function icon(name, cls = '') {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('fill', 'none');
  svg.setAttribute('stroke', 'currentColor');
  svg.setAttribute('stroke-width', '1.8');
  svg.setAttribute('stroke-linecap', 'round');
  svg.setAttribute('stroke-linejoin', 'round');
  svg.setAttribute('aria-hidden', 'true');
  svg.setAttribute('class', `icon ${cls}`.trim());
  svg.innerHTML = ICONS[name] ?? ICONS.file;
  return svg;
}

export function avatar(user, cls = '') {
  const initials = (user?.name || user?.handle || '?').split(/\s+/).map((w) => w[0]).join('').slice(0, 2).toUpperCase();
  const el = h('span', { class: `avatar ${cls}`.trim(), title: user ? user.name : null });
  if (user?.avatarUrl) {
    const img = h('img', { src: user.avatarUrl, alt: '', referrerpolicy: 'no-referrer' });
    img.addEventListener('error', () => clear(el, initials));
    el.append(img);
  } else {
    el.append(initials);
  }
  return el;
}

export function person(user, extra = null) {
  return h('div', { class: 'person' }, avatar(user), h('div', { class: 'who' },
    h('div', {}, user?.name ?? 'Someone'),
    extra ? h('div', { class: 'muted small' }, extra) : null));
}

// ── Toasts ────────────────────────────────────────────────────────────────

let toastBox = null;

export function toast(message, { error = false, ms = 3500 } = {}) {
  if (!toastBox) {
    toastBox = h('div', { class: 'toasts', role: 'status', 'aria-live': 'polite' });
    document.body.append(toastBox);
  }
  const el = h('div', { class: `toast${error ? ' error' : ''}` }, message);
  toastBox.append(el);
  if (ms) {
    setTimeout(() => el.remove(), ms);
  }
  return el;
}

export function showError(err) {
  toast(err?.message ?? String(err), { error: true, ms: 6000 });
}

// ── Dialogs ───────────────────────────────────────────────────────────────

/**
 * Opens a modal dialog. `build(close)` returns its content; resolves with
 * what close() is given (undefined when dismissed).
 */
export function dialog(build, { wide = false } = {}) {
  return new Promise((resolve) => {
    const el = h('dialog', { class: wide ? 'wide' : null });
    let result;
    const close = (value) => {
      result = value;
      el.close();
    };
    el.addEventListener('close', () => {
      el.remove();
      resolve(result);
    });
    el.addEventListener('click', (e) => {
      if (e.target === el) {
        el.close();
      }
    });
    el.append(build(close));
    document.body.append(el);
    el.showModal();
    el.querySelector('[autofocus]')?.focus();
  });
}

/**
 * A dialog with a form; `onSubmit(data)` may throw to keep it open with the
 * message. With `valid(form)`, the submit button is on only while it says
 * yes, checked as anything is typed (and on a 'recheck' event).
 */
export function formDialog({ title, fields, submit = 'Save', note = null, danger = false, onSubmit, valid = null }) {
  return dialog((close) => {
    const error = h('div', { class: 'status-bad small hidden' });
    const form = h('form', { method: 'dialog' },
      h('h2', {}, title),
      fields,
      note ? h('div', { class: 'note' }, note) : null,
      error,
      h('div', { class: 'buttons' },
        h('button', { type: 'button', class: 'btn', onclick: () => close() }, 'Cancel'),
        h('button', { type: 'submit', class: `btn ${danger ? 'danger' : 'primary'}`, disabled: Boolean(valid) }, submit)));
    if (valid) {
      const recheck = () => {
        form.querySelector('button[type="submit"]').disabled = !valid(form);
      };
      form.addEventListener('input', recheck);
      form.addEventListener('recheck', recheck);
    }
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      if (valid && !valid(form)) {
        return;
      }
      const data = Object.fromEntries(new FormData(form).entries());
      for (const box of form.querySelectorAll('input[type="checkbox"][name]')) {
        data[box.name] = box.checked;
      }
      try {
        close((await onSubmit?.(data)) ?? data);
      } catch (err) {
        clear(error, err.message);
        error.classList.remove('hidden');
      }
    });
    return form;
  });
}

export function confirmDialog({ title, message, confirm = 'OK', danger = false }) {
  return formDialog({ title, fields: h('p', { class: 'muted flush' }, message), submit: confirm, danger, onSubmit: () => true })
    .then((v) => v === true);
}

export function promptDialog({ title, label, value = '', submit = 'Save' }) {
  return formDialog({
    title,
    submit,
    fields: h('label', { class: 'field' }, h('span', {}, label), h('input', { type: 'text', name: 'value', value, autofocus: true, required: true, maxlength: 120 })),
  }).then((data) => data?.value?.trim() || null);
}

// ── Pop-up menus ──────────────────────────────────────────────────────────

let openMenu = null;

export function closeMenu() {
  openMenu?.remove();
  openMenu = null;
}

/**
 * A menu under `anchor` (or at an event's point). Items: { label, icon,
 * danger, onclick } objects, '-' for a divider, or { head } for a caption.
 */
export function popup(anchor, items) {
  closeMenu();
  const menu = h('div', { class: 'popup', role: 'menu' });
  for (const item of items.filter(Boolean)) {
    if (item === '-') {
      menu.append(h('hr'));
    } else if (item.head) {
      menu.append(h('div', { class: 'head' }, item.head));
    } else {
      menu.append(h('button', {
        type: 'button',
        role: 'menuitem',
        class: item.danger ? 'danger' : null,
        onclick: (e) => {
          e.stopPropagation();
          closeMenu();
          item.onclick();
        },
      }, item.icon ? icon(item.icon) : null, item.label));
    }
  }
  document.body.append(menu);
  const r = anchor instanceof Event ? { left: anchor.clientX, right: anchor.clientX, bottom: anchor.clientY, top: anchor.clientY } : anchor.getBoundingClientRect();
  const w = menu.offsetWidth;
  const ht = menu.offsetHeight;
  let left = anchor instanceof Event ? r.left : r.right - w;
  let top = r.bottom + 4;
  left = Math.max(8, Math.min(left, innerWidth - w - 8));
  if (top + ht > innerHeight - 8) {
    top = Math.max(8, r.top - ht - 4);
  }
  menu.style.left = `${left}px`;
  menu.style.top = `${top}px`;
  menu.querySelector('button')?.focus();
  openMenu = menu;
}

addEventListener('click', (e) => {
  if (openMenu && !openMenu.contains(e.target)) {
    closeMenu();
  }
}, true);
addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    closeMenu();
  }
});
addEventListener('resize', closeMenu);

// ── Formatting ────────────────────────────────────────────────────────────

export function timeAgo(ms) {
  if (!ms) {
    return '';
  }
  const s = Math.round((Date.now() - ms) / 1000);
  if (s < 60) {
    return 'just now';
  }
  const steps = [[60, 'minute'], [3600, 'hour'], [86400, 'day'], [604800, 'week']];
  for (let i = steps.length - 1; i >= 0; i--) {
    const [size, unit] = steps[i];
    if (s >= size) {
      if (unit === 'week' && s > 4 * 604800) {
        break;
      }
      const n = Math.floor(s / size);
      return `${n} ${unit}${n === 1 ? '' : 's'} ago`;
    }
  }
  return new Date(ms).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' });
}

export function dateTime(ms) {
  return ms ? new Date(ms).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' }) : '';
}

export function bytes(n) {
  if (n < 1024) {
    return `${n} B`;
  }
  const units = ['KB', 'MB', 'GB', 'TB'];
  let v = n / 1024;
  let i = 0;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i++;
  }
  return `${v < 10 ? v.toFixed(1) : Math.round(v)} ${units[i]}`;
}

export async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    toast('Copied.');
  } catch {
    toast('Copy it from the box.');
  }
}
