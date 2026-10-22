// File views: the listings (Recents, My Files, a group's files, ...), cards
// and their menus, uploads, moving, the share dialog and the document page.

import { del, get, patch, post, put, send } from './api.js';
import { navigate, place, rerender, setView, state } from './app.js';
import {
  avatar, bytes, clear, confirmDialog, copyText, dateTime, dialog, formDialog, h, icon, person, popup, promptDialog, showError, timeAgo, toast,
} from './ui.js';

const DOC_TYPE = 'application/x-phoenix-document';

const VISIBILITY = {
  private: { icon: 'lock', label: 'Private', help: 'Only people and groups it is shared with.' },
  unlisted: { icon: 'link', label: 'Unlisted', help: 'Anyone with its address can view it. Not listed anywhere.' },
  public: { icon: 'globe', label: 'Public', help: 'Anyone can find and view it.' },
  showcase: { icon: 'sparkle', label: 'Showcase', help: 'Public, and featured in the showcase as a play-only piece.' },
};

const ROLE_LABELS = { viewer: 'Can view', commenter: 'Can comment', editor: 'Can edit', owner: 'Owner' };
const YOUR_ROLE = { viewer: 'you can view it', commenter: 'you can comment', editor: 'you can edit it', owner: 'you own it' };

function atLeast(role, needed) {
  const order = ['none', 'viewer', 'commenter', 'editor', 'owner'];
  return order.indexOf(role) >= order.indexOf(needed);
}

function isMine(doc) {
  return doc.owner.kind === 'user' && doc.owner.id === state.me.user.id;
}

function ownerName(doc) {
  if (isMine(doc)) {
    return 'You';
  }
  return doc.owner.kind === 'group' ? doc.owner.name : doc.owner.name ?? `@${doc.owner.handle}`;
}

function download(url) {
  const a = h('a', { href: url, download: '' });
  document.body.append(a);
  a.click();
  a.remove();
}

function thumbnail(doc, cls = 'placeholder') {
  if (!doc.thumbnailUrl) {
    return icon('file', cls);
  }
  const img = h('img', { src: doc.thumbnailUrl, alt: '', loading: 'lazy' });
  img.addEventListener('error', () => img.replaceWith(icon('file', cls)));
  return img;
}

// ── Uploads ───────────────────────────────────────────────────────────────

export async function uploadFiles(files) {
  const target = place.editable ? place : { groupId: null, folderId: null };
  let made = 0;
  for (const file of files) {
    if (/\.fla$/i.test(file.name)) {
      toast(`${file.name}: open .fla files in Phoenix and save them as .phx to upload them.`, { error: true, ms: 7000 });
      continue;
    }
    const note = toast(`Uploading ${file.name}…`, { ms: 0 });
    const query = new URLSearchParams({ name: file.name });
    if (target.folderId) {
      query.set('folderId', target.folderId);
    }
    if (target.groupId) {
      query.set('groupId', target.groupId);
    }
    try {
      await send('POST', `/api/documents/upload?${query}`, file, (p) => {
        note.textContent = `Uploading ${file.name}… ${Math.round(p * 100)}%`;
      });
      made++;
    } catch (err) {
      showError(new Error(`${file.name}: ${err.message}`));
    } finally {
      note.remove();
    }
  }
  if (made) {
    toast(made === 1 ? 'Uploaded.' : `Uploaded ${made} files.`);
    if (!place.editable && !location.pathname.startsWith('/files')) {
      navigate('/files');
    } else {
      rerender();
    }
  }
}

let dragDepth = 0;
let dropzone = null;

function hasFiles(e) {
  return [...(e.dataTransfer?.types ?? [])].includes('Files');
}

addEventListener('dragenter', (e) => {
  if (!place.drop || !hasFiles(e)) {
    return;
  }
  dragDepth++;
  if (!dropzone) {
    dropzone = h('div', { class: 'dropzone' }, 'Drop .phx files to upload them here');
    document.body.append(dropzone);
  }
});
addEventListener('dragleave', (e) => {
  if (dropzone && hasFiles(e) && --dragDepth <= 0) {
    dropzone.remove();
    dropzone = null;
    dragDepth = 0;
  }
});
addEventListener('dragover', (e) => {
  if (place.drop && hasFiles(e)) {
    e.preventDefault();
  }
});
addEventListener('drop', (e) => {
  if (!hasFiles(e)) {
    return;
  }
  e.preventDefault();
  dropzone?.remove();
  dropzone = null;
  dragDepth = 0;
  if (place.drop) {
    uploadFiles([...e.dataTransfer.files]);
  }
});

/** Lets documents be dragged onto `el` to move them into `folderId` (null: the top). */
function dropTarget(el, folderId) {
  el.addEventListener('dragover', (e) => {
    if ([...e.dataTransfer.types].includes(DOC_TYPE)) {
      e.preventDefault();
      el.classList.add('drop');
    }
  });
  el.addEventListener('dragleave', () => el.classList.remove('drop'));
  el.addEventListener('drop', async (e) => {
    const id = e.dataTransfer.getData(DOC_TYPE);
    el.classList.remove('drop');
    if (!id) {
      return;
    }
    e.preventDefault();
    e.stopPropagation();
    try {
      await patch(`/api/documents/${id}`, { folderId });
      rerender();
    } catch (err) {
      showError(err);
    }
  });
}

// ── Menus and dialogs for one document ────────────────────────────────────

async function rename(doc) {
  const name = await promptDialog({ title: 'Rename', label: 'Name', value: doc.name });
  if (name && name !== doc.name) {
    await patch(`/api/documents/${doc.id}`, { name });
    rerender();
  }
}

/** The folders of a space as an indented list of options, with the top first. */
async function folderOptions(groupId, selected) {
  const { folders } = await get(`/api/folders${groupId ? `?group=${encodeURIComponent(groupId)}` : ''}`);
  const children = new Map();
  for (const f of folders) {
    const key = f.parentId ?? '';
    children.set(key, [...(children.get(key) ?? []), f]);
  }
  const out = [h('option', { value: '', selected: !selected }, groupId ? 'Top of the group' : 'Top of My Files')];
  const walk = (parent, depth) => {
    for (const f of children.get(parent) ?? []) {
      out.push(h('option', { value: f.id, selected: f.id === selected }, `${' '.repeat(depth)}${f.name}`));
      walk(f.id, depth + 1);
    }
  };
  walk('', 1);
  return out;
}

async function move(doc) {
  const groupId = doc.owner.kind === 'group' ? doc.owner.id : null;
  const options = await folderOptions(groupId, doc.folderId);
  const result = await formDialog({
    title: `Move “${doc.name}”`,
    submit: 'Move',
    fields: h('label', { class: 'field' }, h('span', {}, 'Folder'), h('select', { name: 'folder' }, options)),
    onSubmit: (data) => patch(`/api/documents/${doc.id}`, { folderId: data.folder || null }),
  });
  if (result) {
    rerender();
  }
}

async function duplicate(doc) {
  const { document: copy } = await post(`/api/documents/${doc.id}/duplicate`, {});
  toast(`Made “${copy.name}” in My Files.`);
  rerender();
}

async function toggleFavorite(doc) {
  if (doc.favorite) {
    await del(`/api/favorites/document/${doc.id}`);
  } else {
    await put(`/api/favorites/document/${doc.id}`);
  }
  doc.favorite = !doc.favorite;
}

async function trash(doc) {
  await del(`/api/documents/${doc.id}`);
  toast(`Moved “${doc.name}” to the trash.`);
}

function docMenu(doc, { view, onDone = rerender }) {
  const run = (fn) => async () => {
    try {
      await fn();
    } catch (err) {
      showError(err);
    }
  };
  if (view === 'trash') {
    return [
      { label: 'Restore', icon: 'restore', onclick: run(async () => { await post(`/api/documents/${doc.id}/restore`); toast('Restored.'); onDone(); }) },
      '-',
      {
        label: 'Delete forever', icon: 'trash', danger: true, onclick: run(async () => {
          if (await confirmDialog({ title: 'Delete forever?', message: `“${doc.name}” and all its versions will be gone for good.`, confirm: 'Delete forever', danger: true })) {
            await del(`/api/documents/${doc.id}?purge=1`);
            onDone();
          }
        }),
      },
    ];
  }
  const editor = atLeast(doc.role, 'editor');
  return [
    { label: 'Open', icon: 'file', onclick: () => navigate(`/d/${doc.id}`) },
    doc.canDownload && doc.revision ? { label: 'Download', icon: 'download', onclick: () => download(`/api/documents/${doc.id}/file`) } : null,
    editor ? { label: 'Share…', icon: 'share', onclick: run(() => shareDialog(doc)) } : null,
    '-',
    editor ? { label: 'Rename…', icon: 'edit', onclick: run(() => rename(doc)) } : null,
    editor && (isMine(doc) || doc.owner.kind === 'group') ? { label: 'Move…', icon: 'move', onclick: run(() => move(doc)) } : null,
    doc.allowDuplicate || editor ? { label: 'Make a copy', icon: 'copy', onclick: run(() => duplicate(doc)) } : null,
    { label: doc.favorite ? 'Remove from favorites' : 'Add to favorites', icon: 'star', onclick: run(async () => { await toggleFavorite(doc); onDone(); }) },
    { label: 'Version history', icon: 'history', onclick: () => navigate(`/d/${doc.id}#versions`) },
    view === 'shared' ? '-' : null,
    view === 'shared' ? {
      label: 'Remove from Shared with me', icon: 'x', onclick: run(async () => {
        await del(`/api/documents/${doc.id}/shares/user/${state.me.user.id}`);
        onDone();
      }),
    } : null,
    doc.role === 'owner' ? '-' : null,
    doc.role === 'owner' ? { label: 'Move to trash', icon: 'trash', danger: true, onclick: run(async () => { await trash(doc); onDone(); }) } : null,
  ].filter((item, i, all) => item !== null && !(item === '-' && (i === 0 || all.slice(i + 1).every((x) => x === null || x === '-'))));
}

// ── The share dialog ──────────────────────────────────────────────────────

function roleSelect(value, onchange, roles = ['viewer', 'commenter', 'editor']) {
  const select = h('select', { 'aria-label': 'Role' }, roles.map((r) => h('option', { value: r, selected: r === value }, ROLE_LABELS[r])));
  select.addEventListener('change', () => onchange(select.value));
  return select;
}

export function shareDialog(doc) {
  return dialog((close) => {
    const body = h('div', { class: 'dialog-body' });
    const render = (shares, freshLink = null) => {
      const manage = shares.canManage;
      const act = (fn) => async (...args) => {
        try {
          render(await fn(...args));
        } catch (err) {
          showError(err);
        }
      };

      const who = h('input', { type: 'text', placeholder: 'Handle or email', 'aria-label': 'Who', required: true });
      const kind = h('select', { 'aria-label': 'Person or group' }, h('option', { value: 'user' }, 'Person'), h('option', { value: 'group' }, 'Group'));
      const groupPick = h('select', { 'aria-label': 'Group', class: 'hidden' }, state.me.groups.map((g) => h('option', { value: g.id }, g.name)));
      kind.addEventListener('change', () => {
        who.classList.toggle('hidden', kind.value === 'group');
        groupPick.classList.toggle('hidden', kind.value !== 'group');
      });
      let role = 'viewer';
      const addForm = h('form', { class: 'row' }, kind, who, groupPick, roleSelect(role, (r) => { role = r; }), h('button', { type: 'submit', class: 'btn primary' }, 'Share'));
      addForm.addEventListener('submit', (e) => {
        e.preventDefault();
        act(() => post(`/api/documents/${doc.id}/shares`, { kind: kind.value, who: kind.value === 'group' ? groupPick.value : who.value.trim(), role }))();
      });

      const entries = [
        h('div', { class: 'entry' }, shares.owner.kind === 'group'
          ? h('div', { class: 'person' }, h('span', { class: 'avatar' }, icon('users')), h('div', { class: 'who' }, h('div', {}, shares.owner.name), h('div', { class: 'muted small' }, 'Group that owns it')))
          : person(shares.owner), h('span', { class: 'muted small' }, 'Owner')),
        ...shares.users.map((s) => h('div', { class: 'entry' }, person(s.user),
          manage ? roleSelect(s.role, act((r) => patch(`/api/documents/${doc.id}/shares/user/${s.user.id}`, { role: r }))) : h('span', { class: 'muted small' }, ROLE_LABELS[s.role]),
          manage ? h('button', { type: 'button', class: 'btn ghost icon-only', 'aria-label': 'Remove', title: 'Remove', onclick: act(() => del(`/api/documents/${doc.id}/shares/user/${s.user.id}`)) }, icon('x')) : null)),
        ...shares.groups.map((s) => h('div', { class: 'entry' },
          h('div', { class: 'person' }, h('span', { class: 'avatar' }, icon('users')), h('div', { class: 'who' }, h('div', {}, s.group?.name ?? 'A group'), h('div', { class: 'muted small' }, s.group ? `@${s.group.handle}` : ''))),
          manage ? roleSelect(s.role, act((r) => patch(`/api/documents/${doc.id}/shares/group/${s.group.id}`, { role: r }))) : h('span', { class: 'muted small' }, ROLE_LABELS[s.role]),
          manage ? h('button', { type: 'button', class: 'btn ghost icon-only', 'aria-label': 'Remove', title: 'Remove', onclick: act(() => del(`/api/documents/${doc.id}/shares/group/${s.group.id}`)) }, icon('x')) : null)),
      ];

      const visibility = h('select', { 'aria-label': 'General access', disabled: !manage },
        Object.entries(VISIBILITY).map(([v, info]) => h('option', { value: v, selected: v === shares.visibility }, info.label)));
      const help = h('div', { class: 'muted small' }, VISIBILITY[shares.visibility].help);
      visibility.addEventListener('change', act(async () => {
        await patch(`/api/documents/${doc.id}`, { visibility: visibility.value });
        doc.visibility = visibility.value;
        return get(`/api/documents/${doc.id}/shares`);
      }));
      const flag = (key, label) => {
        const box = h('input', { type: 'checkbox', checked: shares[key], disabled: !manage });
        box.addEventListener('change', act(async () => {
          await patch(`/api/documents/${doc.id}`, { [key]: box.checked });
          return get(`/api/documents/${doc.id}/shares`);
        }));
        return h('label', { class: 'check' }, box, label);
      };

      let linkRole = 'viewer';
      const days = h('select', { 'aria-label': 'Expires' }, [['', 'Never expires'], ['1', 'For a day'], ['7', 'For a week'], ['30', 'For a month']].map(([v, l]) => h('option', { value: v }, l)));
      const links = manage ? [
        h('hr'),
        h('h2', {}, 'Links'),
        h('div', { class: 'muted small' }, 'Anyone with a link gets its access.', state.me.settings.beta ? ' During the beta they also have to be signed in.' : ''),
        freshLink ? h('div', { class: 'copy-row' }, h('input', { type: 'text', value: freshLink, readOnly: true, onfocus: (e) => e.target.select() }),
          h('button', { type: 'button', class: 'btn', onclick: () => copyText(freshLink) }, icon('copy'), 'Copy')) : null,
        freshLink ? h('div', { class: 'muted small' }, 'Copy it now: it is shown only once.') : null,
        ...shares.links.map((l) => h('div', { class: 'row' }, icon('link'), h('span', { class: 'grow' }, `${ROLE_LABELS[l.role]} · made ${timeAgo(l.createdAt)}${l.expiresAt ? ` · until ${dateTime(l.expiresAt)}` : ''}`),
          h('button', { type: 'button', class: 'btn ghost', onclick: act(async () => { await del(`/api/documents/${doc.id}/links/${l.id}`); return get(`/api/documents/${doc.id}/shares`); }) }, 'Turn off'))),
        h('div', { class: 'row' }, roleSelect(linkRole, (r) => { linkRole = r; }, ['viewer', 'editor']), days,
          h('button', {
            type: 'button', class: 'btn', onclick: async () => {
              try {
                const made = await post(`/api/documents/${doc.id}/links`, { role: linkRole, days: days.value ? Number(days.value) : null });
                render(await get(`/api/documents/${doc.id}/shares`), made.url);
              } catch (err) {
                showError(err);
              }
            },
          }, icon('link'), 'Make a link')),
      ] : [];

      clear(body,
        h('h2', {}, `Share “${doc.name}”`),
        manage ? addForm : h('div', { class: 'muted small' }, 'Only its owner can change who it is shared with.'),
        h('div', { class: 'people' }, entries),
        h('hr'),
        h('h2', {}, 'General access'),
        h('div', { class: 'row' }, icon(VISIBILITY[shares.visibility].icon), visibility),
        help,
        state.me.settings.beta && shares.visibility !== 'private' ? h('div', { class: 'muted small' }, 'During the beta, only signed-in members can see it.') : null,
        flag('allowDownload', 'Viewers can download it and open it in the editor'),
        flag('allowDuplicate', 'Viewers can make their own copy'),
        links,
        h('div', { class: 'buttons' }, h('button', { type: 'button', class: 'btn primary', onclick: () => close(true) }, 'Done')));
    };
    clear(body, h('div', { class: 'muted' }, 'Loading…'));
    get(`/api/documents/${doc.id}/shares`).then((s) => render(s)).catch((err) => {
      showError(err);
      close();
    });
    return body;
  }, { wide: true }).then(() => rerender());
}

// ── Listings ──────────────────────────────────────────────────────────────

function badge(doc, view) {
  if (view === 'trash') {
    return h('span', { class: 'badge' }, `Deleted ${timeAgo(doc.trashedAt)}`);
  }
  if (doc.visibility === 'private') {
    return null;
  }
  const v = VISIBILITY[doc.visibility];
  return h('span', { class: 'badge accent' }, icon(v.icon), v.label);
}

function meta(doc) {
  const by = doc.modifiedBy && doc.modifiedBy.id !== state.me.user.id ? ` by ${doc.modifiedBy.name}` : '';
  const owner = isMine(doc) ? '' : `${ownerName(doc)} · `;
  return doc.revision ? `${owner}Edited ${timeAgo(doc.modifiedAt)}${by}` : `${owner}Empty`;
}

function card(doc, view, movable) {
  const more = h('button', { type: 'button', class: 'btn ghost icon-only', 'aria-label': 'More', title: 'More' }, icon('more'));
  const star = view === 'trash' ? null : h('button', { type: 'button', class: `star${doc.favorite ? ' on' : ''}`, 'aria-label': doc.favorite ? 'Remove from favorites' : 'Add to favorites', title: 'Favorite' }, icon('star'));
  const el = h('div', { class: 'card', tabindex: 0, role: 'link', 'aria-label': doc.name, draggable: movable ? 'true' : null },
    h('div', { class: 'thumb' }, thumbnail(doc), badge(doc, view)),
    star,
    h('div', { class: 'info' }, h('div', { class: 'text' }, h('div', { class: 'title' }, doc.name), h('div', { class: 'meta' }, meta(doc))), more));
  const open = () => (view === 'trash' ? null : navigate(`/d/${doc.id}`));
  el.addEventListener('click', (e) => {
    if (e.target.closest('button')) {
      return;
    }
    open();
  });
  el.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && e.target === el) {
      open();
    }
  });
  el.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    popup(e, docMenu(doc, { view }));
  });
  more.addEventListener('click', () => popup(more, docMenu(doc, { view })));
  star?.addEventListener('click', async () => {
    try {
      await toggleFavorite(doc);
      star.classList.toggle('on', doc.favorite);
      if (view === 'favorites' && !doc.favorite) {
        el.remove();
      }
    } catch (err) {
      showError(err);
    }
  });
  if (movable) {
    el.addEventListener('dragstart', (e) => {
      e.dataTransfer.setData(DOC_TYPE, doc.id);
      e.dataTransfer.effectAllowed = 'move';
      el.classList.add('dragging');
    });
    el.addEventListener('dragend', () => el.classList.remove('dragging'));
  }
  return el;
}

function listItem(doc, view) {
  const more = h('button', { type: 'button', class: 'btn ghost icon-only', 'aria-label': 'More' }, icon('more'));
  more.addEventListener('click', (e) => {
    e.stopPropagation();
    popup(more, docMenu(doc, { view }));
  });
  const el = h('div', { class: 'item', role: 'link', tabindex: 0 },
    h('div', { class: 'mini' }, thumbnail(doc, '')),
    h('div', { class: 'cell' }, h('div', { class: 'title' }, doc.name), h('div', { class: 'muted small' }, view === 'trash' || doc.visibility === 'private' ? '' : VISIBILITY[doc.visibility].label)),
    h('div', { class: 'cell muted wide-only' }, ownerName(doc)),
    h('div', { class: 'cell muted wide-only' }, timeAgo(view === 'trash' ? doc.trashedAt : doc.modifiedAt)),
    h('div', { class: 'cell muted wide-only' }, doc.revision ? bytes(doc.size) : '—'),
    more);
  el.addEventListener('click', () => view !== 'trash' && navigate(`/d/${doc.id}`));
  el.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    popup(e, docMenu(doc, { view }));
  });
  return el;
}

function folderTile(folder, href, editable) {
  const more = editable ? h('button', { type: 'button', class: 'btn ghost icon-only', 'aria-label': 'Folder actions' }, icon('more')) : null;
  const el = h('div', { class: 'folder', tabindex: 0, role: 'link' }, icon('folder'), h('span', { class: 'name' }, folder.name), more);
  el.addEventListener('click', (e) => {
    if (!e.target.closest('button')) {
      navigate(href);
    }
  });
  el.addEventListener('keydown', (e) => e.key === 'Enter' && navigate(href));
  more?.addEventListener('click', () => popup(more, [
    {
      label: 'Rename…', icon: 'edit', onclick: async () => {
        const name = await promptDialog({ title: 'Rename folder', label: 'Name', value: folder.name });
        if (name) {
          await patch(`/api/folders/${folder.id}`, { name }).catch(showError);
          rerender();
        }
      },
    },
    '-',
    {
      label: 'Delete folder', icon: 'trash', danger: true, onclick: async () => {
        if (await confirmDialog({ title: 'Delete folder?', message: `“${folder.name}” and its folders will be deleted, and the documents in them moved to the trash.`, confirm: 'Delete', danger: true })) {
          await del(`/api/folders/${folder.id}`).catch(showError);
          rerender();
        }
      },
    },
  ]));
  if (editable) {
    dropTarget(el, folder.id);
  }
  return el;
}

const EMPTY = {
  recent: ['clock', 'Nothing opened yet', 'Documents you open or save show up here.'],
  files: ['folder', 'No files yet', 'Upload .phx files with New, or drop them here.'],
  group: ['users', 'No files in this group yet', 'Upload .phx files with New, or drop them here.'],
  shared: ['share', 'Nothing shared with you yet', 'When someone shares a document with you, it shows up here.'],
  favorites: ['star', 'No favorites yet', 'Star a document to keep it here.'],
  trash: ['trash', 'The trash is empty', 'Deleted documents wait here until you delete them for good.'],
  search: ['search', 'Nothing found', 'Try another name.'],
};

/**
 * One listing. `view` is recent, files, group, shared, favorites, trash or
 * search; files and group take a folder. `rootHref`/`folderHref` build the
 * breadcrumb links; `before` goes above the heading.
 */
export async function renderListing(target, { view, folder, q, title, groupId, rootHref = '/files', folderHref = (id) => `/files/${id}`, before = null }) {
  const query = new URLSearchParams();
  if (view === 'search') {
    query.set('q', q);
  } else {
    query.set('view', view);
  }
  if (folder) {
    query.set('folder', folder);
  }
  if (groupId) {
    query.set('group', groupId);
  }
  const data = await get(`/api/documents?${query}`);
  if (!target.isConnected) {
    return;
  }
  const spaced = view === 'files' || view === 'group';
  if (spaced) {
    place.groupId = groupId ?? null;
    place.folderId = folder ?? null;
    place.editable = Boolean(data.canEdit);
    place.drop = Boolean(data.canEdit);
  } else {
    place.editable = false;
  }

  let heading;
  if (spaced && data.path.length) {
    const root = h('a', { href: rootHref }, title);
    if (data.canEdit) {
      dropTarget(root, null);
    }
    const parts = [root];
    data.path.forEach((f, i) => {
      parts.push(h('span', { class: 'sep' }, '/'));
      if (i === data.path.length - 1) {
        parts.push(h('span', {}, f.name));
      } else {
        const a = h('a', { href: folderHref(f.id) }, f.name);
        if (data.canEdit) {
          dropTarget(a, f.id);
        }
        parts.push(a);
      }
    });
    heading = h('div', { class: 'crumbs' }, parts);
  } else {
    heading = h('h1', {}, view === 'search' ? `Search: ${q}` : title);
  }

  const viewToggle = h('div', { class: 'row' },
    h('button', { type: 'button', class: `btn ghost icon-only${state.view === 'grid' ? ' active' : ''}`, 'aria-label': 'Grid', title: 'Grid', onclick: () => setView('grid') }, icon('grid')),
    h('button', { type: 'button', class: `btn ghost icon-only${state.view === 'list' ? ' active' : ''}`, 'aria-label': 'List', title: 'List', onclick: () => setView('list') }, icon('list')));

  const fileInput = h('input', { type: 'file', accept: '.phx,.fla', multiple: true, class: 'hidden' });
  fileInput.addEventListener('change', () => uploadFiles([...fileInput.files]));
  const actions = spaced && data.canEdit ? [
    h('button', {
      type: 'button', class: 'btn', onclick: async () => {
        const name = await promptDialog({ title: 'New folder', label: 'Name', value: 'New folder', submit: 'Create' });
        if (name) {
          await post('/api/folders', { name, parentId: folder ?? null, groupId: groupId ?? null }).catch(showError);
          rerender();
        }
      },
    }, icon('folder'), 'New folder'),
    h('button', { type: 'button', class: 'btn', onclick: () => fileInput.click() }, icon('upload'), 'Upload'),
    fileInput,
  ] : [];

  const trashAll = view === 'trash' && data.documents.length ? h('button', {
    type: 'button', class: 'btn danger', onclick: async () => {
      if (await confirmDialog({ title: 'Empty the trash?', message: `${data.documents.length} document(s) will be gone for good.`, confirm: 'Empty trash', danger: true })) {
        for (const d of data.documents) {
          await del(`/api/documents/${d.id}?purge=1`).catch(showError);
        }
        rerender();
      }
    },
  }, 'Empty trash') : null;

  const head = h('div', { class: 'page-head' }, heading, h('div', { class: 'grow' }),
    data.usage ? h('span', { class: 'usage' }, `${bytes(data.usage.bytes)} of ${bytes(data.usage.quota)}`) : null,
    actions, trashAll, viewToggle);

  const folders = data.folders.length
    ? h('div', { class: 'folders' }, data.folders.map((f) => folderTile(f, folderHref(f.id), data.canEdit)))
    : null;

  let docs;
  if (!data.documents.length) {
    const [ic, heading2, text] = EMPTY[view] ?? EMPTY.files;
    docs = data.folders.length ? null : h('div', { class: 'empty' }, icon(ic), h('h2', {}, heading2), h('p', {}, text));
  } else if (state.view === 'list') {
    docs = h('div', { class: 'list' },
      h('div', { class: 'item head' }, h('div'), h('div', {}, 'Name'), h('div', { class: 'wide-only' }, 'Owner'), h('div', { class: 'wide-only' }, view === 'trash' ? 'Deleted' : 'Edited'), h('div', { class: 'wide-only' }, 'Size'), h('div')),
      data.documents.map((d) => listItem(d, view)));
  } else {
    docs = h('div', { class: 'grid' }, data.documents.map((d) => card(d, view, spaced && data.canEdit)));
  }
  clear(target, before, head, folders && data.documents.length ? h('div', { class: 'section-title' }, 'Folders') : null, folders,
    folders && data.documents.length ? h('div', { class: 'section-title' }, 'Files') : null, docs);
}

// ── The document page ─────────────────────────────────────────────────────

function revisionRow(doc, r) {
  const editor = atLeast(doc.role, 'editor');
  const actions = [];
  if (doc.canDownload) {
    actions.push({ label: 'Download this version', icon: 'download', onclick: () => download(`/api/documents/${doc.id}/file?rev=${r.number}`) });
  }
  if (editor) {
    actions.push({
      label: r.label ? 'Rename version…' : 'Name this version…', icon: 'edit', onclick: async () => {
        const label = await promptDialog({ title: 'Name this version', label: 'Name', value: r.label });
        if (label !== null) {
          await patch(`/api/documents/${doc.id}/revisions/${r.number}`, { label }).catch(showError);
          rerender();
        }
      },
    });
    if (r.number !== doc.revision) {
      actions.push({
        label: 'Restore this version', icon: 'restore', onclick: async () => {
          try {
            await post(`/api/documents/${doc.id}/revisions/${r.number}/restore`);
            toast(`Restored version ${r.number} as a new version.`);
            rerender();
          } catch (err) {
            showError(err);
          }
        },
      });
    }
  }
  const more = actions.length ? h('button', { type: 'button', class: 'btn ghost icon-only', 'aria-label': 'Version actions', onclick: (e) => popup(e.currentTarget, actions) }, icon('more')) : null;
  return h('div', { class: 'revision' },
    h('span', { class: 'num' }, `v${r.number}`),
    h('div', { class: 'what' },
      r.label ? h('div', { class: 'label' }, r.label) : null,
      h('div', { class: 'row small' }, avatar(r.createdBy, 'small'), h('span', {}, `${r.createdBy?.name ?? 'Someone'} · ${timeAgo(r.createdAt)}`)),
      h('div', { class: 'muted small' }, `${bytes(r.size)}${r.application ? ` · ${r.application}` : ''}${r.number === doc.revision ? ' · current' : ''}`)),
    more);
}

export async function renderDocument(target, id) {
  const [{ document: doc }, { revisions }] = await Promise.all([get(`/api/documents/${id}`), get(`/api/documents/${id}/revisions`)]);
  if (!target.isConnected) {
    return;
  }
  post(`/api/documents/${id}/opened`).catch(() => {});
  const editor = atLeast(doc.role, 'editor');

  const title = h('input', { class: 'doc-title', value: doc.name, readOnly: !editor, 'aria-label': 'Name', maxlength: 120 });
  title.addEventListener('keydown', (e) => e.key === 'Enter' && title.blur());
  title.addEventListener('change', async () => {
    const name = title.value.trim();
    if (!name) {
      title.value = doc.name;
      return;
    }
    try {
      await patch(`/api/documents/${doc.id}`, { name });
      doc.name = name;
      toast('Renamed.');
    } catch (err) {
      showError(err);
    }
  });
  const description = editor
    ? h('textarea', { rows: 2, placeholder: 'Add a description', 'aria-label': 'Description', value: doc.description })
    : (doc.description ? h('p', {}, doc.description) : null);
  if (editor) {
    description.addEventListener('change', () => patch(`/api/documents/${doc.id}`, { description: description.value }).catch(showError));
  }

  const v = VISIBILITY[doc.visibility];
  const actions = h('div', { class: 'actions' },
    doc.canDownload && doc.revision ? h('button', { type: 'button', class: 'btn primary', onclick: () => download(`/api/documents/${doc.id}/file`) }, icon('download'), 'Download') : null,
    editor ? h('button', { type: 'button', class: 'btn', onclick: () => shareDialog(doc) }, icon('share'), 'Share') : null,
    doc.allowDuplicate || editor ? h('button', { type: 'button', class: 'btn', onclick: () => duplicate(doc).catch(showError) }, icon('copy'), 'Make a copy') : null,
    h('button', {
      type: 'button', class: 'btn', onclick: async (e) => {
        const btn = e.currentTarget;
        try {
          await toggleFavorite(doc);
          clear(btn, icon('star'), doc.favorite ? 'Favorited' : 'Favorite');
        } catch (err) {
          showError(err);
        }
      },
    }, icon('star'), doc.favorite ? 'Favorited' : 'Favorite'),
    h('button', { type: 'button', class: 'btn ghost icon-only', 'aria-label': 'More', onclick: (e) => popup(e.currentTarget, docMenu(doc, { view: 'doc', onDone: () => (doc.role === 'owner' ? navigate('/files') : rerender()) })) }, icon('more')));

  const info = doc.info ?? {};
  const owner = doc.owner.kind === 'group'
    ? h('a', { href: `/groups/${doc.owner.id}` }, doc.owner.name)
    : (isMine(doc) ? 'You' : `${doc.owner.name} (@${doc.owner.handle})`);
  const details = h('div', { class: 'panel' }, h('h2', {}, 'Details'), h('dl', { class: 'props' },
    h('dt', {}, 'Owner'), h('dd', {}, owner),
    h('dt', {}, 'Access'), h('dd', {}, `${v.label} · ${YOUR_ROLE[doc.role] ?? doc.role}`),
    h('dt', {}, 'Size'), h('dd', {}, doc.revision ? bytes(doc.size) : 'Nothing saved yet'),
    h('dt', {}, 'Edited'), h('dd', {}, `${dateTime(doc.modifiedAt)}${doc.modifiedBy ? ` by ${doc.modifiedBy.name}` : ''}`),
    h('dt', {}, 'Created'), h('dd', {}, dateTime(doc.createdAt)),
    info.application ? [h('dt', {}, 'Saved with'), h('dd', {}, info.application)] : null,
    info.author ? [h('dt', {}, 'Author'), h('dd', {}, info.author)] : null,
    doc.forkedFrom ? [h('dt', {}, 'Copied from'), h('dd', {}, h('a', { href: `/d/${doc.forkedFrom}` }, 'the original'))] : null));

  const versions = h('div', { class: 'panel', id: 'versions' }, h('h2', {}, 'Versions'),
    revisions.length ? h('div', { class: 'revisions' }, revisions.map((r) => revisionRow(doc, r))) : h('div', { class: 'muted' }, 'Nothing saved yet.'));

  clear(target,
    h('div', { class: 'doc' },
      h('div', { class: 'stack' },
        h('div', { class: 'preview' }, thumbnail(doc, '')),
        title,
        description,
        actions),
      h('div', { class: 'stack' }, details, versions)));
  if (location.hash === '#versions') {
    versions.scrollIntoView();
  }
}
