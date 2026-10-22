// The cloud's part of the editor page (/editor/?doc=ID, &play=1 to play,
// &tutorial=LESSON to run one of Phoenix's tutorials on it).
// Phoenix's page asks window.phoenixHost to prepare it before it starts
// (docs/web-editor.md, "A page that keeps documents"); this:
//   - fetches the document and the font library into Phoenix's file system
//     and starts Phoenix on it, showing its picture while Phoenix loads;
//   - takes Phoenix's saves as new versions, and its drafts (unsaved work,
//     every two minutes) to offer back if the page closes before a save;
//   - holds the document's edit lock while editing, so two people don't
//     save over each other;
//   - shows a bar above Phoenix: where the document is, whether there are
//     unsaved changes and whether the last save reached the cloud;
//   - lets Phoenix open another cloud document's library.

(() => {
  const params = new URLSearchParams(location.search);
  if (!params.get('doc')) {
    return;
  }
  const state = {
    id: params.get('doc'),
    link: params.get('link'),
    play: params.get('play') === '1',
    tutorial: params.get('tutorial'),
    path: null,
    revision: 0,
    role: 'viewer',
    name: '',
    csrf: null,
    saving: false,
    queued: null,
    unsent: null,
    instance: null,
    unsaved: false,
    lockedBy: null,
    fromDraft: false,
  };
  const LOCK_EVERY_MS = 30 * 1000;

  const style = document.createElement('style');
  style.textContent = `
    #cloudbar { position: fixed; top: 0; left: 0; right: 0; height: 34px; display: flex; align-items: center; gap: 12px;
      padding: 0 12px; background: #1b1b1e; color: #e9e9ec; border-bottom: 1px solid #2d2d32; z-index: 10;
      font: 13px/1 system-ui, -apple-system, "Segoe UI", sans-serif; box-sizing: border-box; }
    #cloudbar a { color: inherit; text-decoration: none; }
    #cloudbar .home { display: flex; align-items: center; gap: 8px; font-weight: 600; }
    #cloudbar .home img { width: 20px; height: 20px; }
    #cloudbar .name { font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    #cloudbar .sep { color: #6b6e76; }
    #cloudbar .status { color: #a2a5ad; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    #cloudbar .status.bad { color: #ff8a80; }
    #cloudbar .status.good { color: #4cc9a0; }
    #cloudbar .unsaved { color: #ffb36b; font-weight: 600; }
    #cloudbar .grow { flex: 1; }
    #cloudbar button, #cloudbar .button { height: 24px; padding: 0 10px; border-radius: 6px; border: 1px solid #3a3a40;
      background: #232327; color: inherit; font: inherit; cursor: pointer; display: inline-flex; align-items: center; }
    #cloudbar .primary { background: #ff7a2f; border-color: #ff7a2f; color: #1b0d04; font-weight: 600; }
    #screen { position: absolute; top: 34px; left: 0; right: 0; bottom: 0; width: auto !important; height: auto !important; }
    #loading { top: 34px !important; }
    #loading img.poster { width: min(560px, 80vw); height: auto; max-height: 50vh; object-fit: contain; border-radius: 10px;
      box-shadow: 0 8px 30px rgba(0,0,0,.4); }
    .cloud-overlay { position: fixed; inset: 0; z-index: 20; display: grid; place-items: center; background: rgba(0,0,0,.55);
      font: 14px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; color: #e9e9ec; }
    .cloud-overlay .box { background: #1b1b1e; border: 1px solid #2d2d32; border-radius: 12px; padding: 20px; width: min(560px, calc(100vw - 32px));
      max-height: calc(100vh - 80px); display: flex; flex-direction: column; gap: 12px; }
    .cloud-overlay h2 { margin: 0; font-size: 17px; }
    .cloud-overlay .row { display: flex; gap: 8px; justify-content: flex-end; }
    .cloud-overlay button { height: 32px; padding: 0 14px; border-radius: 8px; border: 1px solid #3a3a40; background: #232327; color: inherit; font: inherit; cursor: pointer; }
    .cloud-overlay button.primary { background: #ff7a2f; border-color: #ff7a2f; color: #1b0d04; font-weight: 600; }
    .cloud-overlay input { height: 32px; padding: 0 10px; border-radius: 8px; border: 1px solid #3a3a40; background: #131315; color: inherit; font: inherit; }
    .cloud-overlay .docs { overflow-y: auto; display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 10px; }
    .cloud-overlay .doc { display: flex; flex-direction: column; gap: 4px; padding: 0; border: none; background: none; text-align: left; height: auto; }
    .cloud-overlay .doc .pic { aspect-ratio: 16 / 10; border-radius: 8px; border: 1px solid #2d2d32; background: #202024; display: grid; place-items: center; overflow: hidden; }
    .cloud-overlay .doc .pic img { width: 100%; height: 100%; object-fit: contain; }
    .cloud-overlay .doc:hover .pic { border-color: #ff7a2f; }
    .cloud-overlay .muted { color: #a2a5ad; font-size: 12px; }
  `;
  document.head.append(style);

  const el = (tag, attrs = {}, ...children) => {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k.startsWith('on')) {
        node.addEventListener(k.slice(2), v);
      } else {
        node.setAttribute(k, v);
      }
    }
    node.append(...children.flat(Infinity).filter((c) => c !== null && c !== undefined));
    return node;
  };

  const status = el('span', { class: 'status' });
  const unsavedMark = el('span', { class: 'unsaved', hidden: '' }, '● Unsaved changes');
  const actions = el('span', { class: 'actions' });
  const nameSpan = el('span', { class: 'name' }, '');
  const modeLink = el('a', { class: 'button' }, '');

  function setStatus(text, kind = '', buttons = []) {
    status.textContent = text;
    status.className = `status ${kind}`;
    actions.replaceChildren(...buttons);
  }

  function pageUrl(play) {
    const q = new URLSearchParams({ doc: state.id });
    if (state.link) {
      q.set('link', state.link);
    }
    if (play) {
      q.set('play', '1');
    }
    return `/editor/?${q}`;
  }

  function detailsUrl() {
    return `/d/${encodeURIComponent(state.id)}${state.link ? `?link=${encodeURIComponent(state.link)}` : ''}`;
  }

  function showBar() {
    nameSpan.textContent = state.name;
    modeLink.textContent = state.play ? 'Edit' : 'Play';
    modeLink.href = state.play ? pageUrl(false) : `/play/${encodeURIComponent(state.id)}`;
    const fullscreen = state.play ? el('button', {
      title: 'Fill the screen',
      onclick: () => (document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen()).catch(() => {}),
    }, 'Full screen') : null;
    const bar = el('div', { id: 'cloudbar' },
      el('a', { class: 'home', href: '/', title: 'Back to your files' }, el('img', { src: '/ui/logo.png', alt: '' }), 'Phoenix Cloud'),
      el('span', { class: 'sep' }, '/'),
      el('a', { href: detailsUrl(), title: 'Details and versions' }, nameSpan),
      unsavedMark,
      status,
      actions,
      el('span', { class: 'grow' }),
      fullscreen,
      modeLink);
    document.body.prepend(bar);
  }

  async function api(method, url, body, type = 'application/json', extra = {}) {
    const headers = {};
    if (state.csrf && method !== 'GET') {
      headers['X-CSRF-Token'] = state.csrf;
    }
    if (state.link) {
      headers['X-Phoenix-Link'] = state.link;
    }
    if (body !== undefined) {
      headers['Content-Type'] = type;
    }
    const res = await fetch(url, { method, headers, body, credentials: 'same-origin', ...extra });
    let json = null;
    if (res.headers.get('content-type')?.startsWith('application/json')) {
      json = await res.json().catch(() => null);
    }
    return { res, json };
  }

  const docUrl = (tail = '') => `/api/documents/${encodeURIComponent(state.id)}${tail}`;

  /** A question in the page before Phoenix starts: resolves with the button's value. */
  function ask(title, text, buttons) {
    return new Promise((resolve) => {
      const overlay = el('div', { class: 'cloud-overlay' }, el('div', { class: 'box' },
        el('h2', {}, title), el('div', {}, text),
        el('div', { class: 'row' }, buttons.map(([label, value, primary]) => el('button', {
          class: primary ? 'primary' : '',
          onclick: () => {
            overlay.remove();
            resolve(value);
          },
        }, label)))));
      document.body.append(overlay);
    });
  }

  /** While the document loads: its picture and name. */
  function showPoster(doc) {
    const loading = document.getElementById('loading');
    if (!loading) {
      return;
    }
    const picture = loading.querySelector('img');
    if (picture && doc.thumbnailUrl) {
      picture.src = doc.thumbnailUrl;
      picture.className = 'poster';
    }
    const title = loading.querySelector('h1');
    if (title) {
      title.textContent = doc.name;
    }
  }

  /** The font library, into Phoenix's file system before it starts. */
  async function fetchFonts() {
    const list = await api('GET', '/api/fonts');
    const fonts = list.json?.fonts ?? [];
    return Promise.all(fonts.map(async (f) => {
      const res = await fetch(f.url, { credentials: 'same-origin' });
      return res.ok ? { name: f.name, bytes: new Uint8Array(await res.arrayBuffer()) } : null;
    })).then((all) => all.filter(Boolean));
  }

  async function prepare(config, loadingStatus) {
    const me = await api('GET', '/api/me');
    if (!me.json?.user) {
      location.href = `/signin?return=${encodeURIComponent(location.pathname + location.search)}`;
      await new Promise(() => {});
    }
    state.csrf = me.json.csrf;
    const info = await api('GET', docUrl());
    if (!info.res.ok) {
      throw new Error(info.json?.message ?? `the document could not be found (${info.res.status})`);
    }
    const doc = info.json.document;
    state.name = doc.name;
    state.role = doc.role;
    state.revision = doc.revision;
    document.title = `${doc.name} · Phoenix`;
    showBar();
    showPoster(doc);
    if (!doc.revision) {
      throw new Error('this document has nothing saved in it yet');
    }
    if (!doc.canDownload) {
      // Only played: the player page shows it without opening it.
      location.replace(`/play/${encodeURIComponent(state.id)}`);
      await new Promise(() => {});
    }
    const editor = doc.role === 'editor' || doc.role === 'owner';

    // Unsaved work kept from last time comes back if they want it.
    let source = docUrl('/file');
    if (!state.play) {
      const { json } = await api('GET', docUrl('/draft')).catch(() => ({ json: null }));
      const draft = json?.draft;
      if (draft) {
        const when = new Date(draft.savedAt).toLocaleString();
        const choice = await ask('Open your unsaved changes?', `You made changes to “${doc.name}” on ${when} that were not saved. Open them, or the last saved version?`,
          [['Last saved version', 'saved'], ['Unsaved changes', 'draft', true]]);
        if (choice === 'draft') {
          source = docUrl('/draft/file');
          state.fromDraft = true;
          // Saves are checked against the version the draft was made from.
          state.revision = draft.baseRevision;
        } else {
          api('DELETE', docUrl('/draft')).catch(() => {});
        }
      }
    }

    loadingStatus.textContent = `Opening ${doc.name}…`;
    const [file, fonts] = await Promise.all([
      fetch(source, { credentials: 'same-origin', headers: state.link ? { 'X-Phoenix-Link': state.link } : {} }),
      fetchFonts().catch(() => []),
    ]);
    if (!file.ok) {
      throw new Error(`the document could not be read (${file.status})`);
    }
    if (!state.fromDraft) {
      state.revision = Number(file.headers.get('X-Phoenix-Revision')) || doc.revision;
    }
    const bytes = new Uint8Array(await file.arrayBuffer());
    state.path = `/cloud/${doc.name.replace(/[\\/:*?"<>|\u0000-\u001f]+/g, '_').slice(0, 100) || 'Untitled'}.phx`;
    // --hosted: the document is this page's, so Phoenix leaves out New,
    // Open, Save As and the like.
    config.arguments = [state.path, '--hosted', ...(state.play ? ['--play'] : []), ...(state.tutorial && !state.play ? ['--tutorial', state.tutorial] : [])];
    if (fonts.length) {
      config.qt.environment = { ...(config.qt.environment ?? {}), PHOENIX_FONTS_DIR: '/fonts' };
    }
    // A reload (or Play and back) does not start the lesson again.
    if (state.tutorial) {
      history.replaceState(null, '', pageUrl(state.play));
    }
    config.preRun = [...(config.preRun ?? []), (instance) => {
      // Phoenix's instance from the start: the loader hands it to started()
      // only some time after Phoenix is up.
      started(instance);
      for (const dir of ['/cloud', '/cloud/libraries', '/fonts']) {
        try {
          instance.FS.mkdir(dir);
        } catch {
          // There already.
        }
      }
      instance.FS.writeFile(state.path, bytes);
      for (const font of fonts) {
        instance.FS.writeFile(`/fonts/${font.name}`, font.bytes);
      }
    }];
    api('POST', docUrl('/opened'), '{}').catch(() => {});
    if (state.play) {
      setStatus('Playing');
    } else if (state.fromDraft) {
      setStatus('Opened your unsaved changes: save to keep them', 'bad');
    } else {
      setStatus(editor ? `Version ${state.revision}` : 'You can view it; save to keep a copy in your files');
    }
    if (editor && !state.play) {
      await keepLock();
      setInterval(keepLock, LOCK_EVERY_MS);
    }
  }

  // ── The edit lock ────────────────────────────────────────────────────────

  async function keepLock() {
    const { json } = await api('POST', docUrl('/lock'), '{}').catch(() => ({ json: null }));
    const holder = json && !json.held ? json.holder : null;
    if ((holder?.name ?? null) !== state.lockedBy) {
      state.lockedBy = holder?.name ?? null;
      if (state.lockedBy) {
        setStatus(`${state.lockedBy} is editing it now: your saves go to a copy`, 'bad');
      } else if (!state.saving && !state.unsent) {
        setStatus(`Version ${state.revision} · you can save here now`, 'good');
      }
    }
  }

  addEventListener('pagehide', () => {
    if (!state.play && !state.lockedBy) {
      // Let go of the lock as the page goes (keepalive outlives the page).
      api('DELETE', docUrl('/lock'), undefined, 'application/json', { keepalive: true }).catch(() => {});
    }
  });

  // ── Saves and drafts ─────────────────────────────────────────────────────

  async function saveCopy(bytes) {
    setStatus('Saving a copy…');
    const { res, json } = await api('POST', `/api/documents/upload?name=${encodeURIComponent(`${state.name} copy.phx`)}`, bytes, 'application/octet-stream');
    if (!res.ok) {
      state.unsent = bytes;
      setStatus(`Not saved: ${json?.message ?? res.status}`, 'bad', [el('button', { onclick: () => saveCopy(bytes) }, 'Try again')]);
      return;
    }
    if (!state.lockedBy) {
      api('DELETE', docUrl('/lock')).catch(() => {});
    }
    state.id = json.document.id;
    state.name = json.document.name;
    state.revision = json.document.revision;
    state.role = 'owner';
    state.unsent = null;
    state.link = null;
    state.lockedBy = null;
    nameSpan.textContent = state.name;
    history.replaceState(null, '', pageUrl(state.play));
    modeLink.href = `/play/${encodeURIComponent(state.id)}`;
    setStatus('Saved as a copy in your files', 'good');
    keepLock();
  }

  async function upload(bytes) {
    state.saving = true;
    setStatus('Saving to the cloud…');
    try {
      const { res, json } = await api('PUT', docUrl(`/file?base=${state.revision}`), bytes, 'application/octet-stream');
      if (res.ok) {
        state.revision = json.revision;
        state.unsent = null;
        state.fromDraft = false;
        setStatus(`Saved · version ${state.revision}`, 'good');
      } else if (res.status === 409 || res.status === 403 || res.status === 423) {
        state.unsent = bytes;
        const why = res.status === 409 ? 'Someone else saved it since you opened it.'
          : res.status === 423 ? (json?.message ?? 'Someone else is editing it.')
            : 'You can view this document but not save over it.';
        setStatus(`Not saved: ${why}`, 'bad', [el('button', { class: 'primary', onclick: () => saveCopy(bytes) }, 'Save a copy')]);
      } else {
        state.unsent = bytes;
        const why = res.status === 401 ? 'you are signed out; sign in in another tab' : (json?.message ?? `the server answered ${res.status}`);
        setStatus(`Not saved: ${why}`, 'bad', [el('button', { onclick: () => save(state.path, bytes) }, 'Try again')]);
      }
    } catch {
      state.unsent = bytes;
      setStatus('Not saved: the server could not be reached', 'bad', [el('button', { onclick: () => save(state.path, bytes) }, 'Try again')]);
    } finally {
      state.saving = false;
      if (state.queued) {
        const next = state.queued;
        state.queued = null;
        upload(next);
      }
    }
  }

  function save(path, bytes) {
    if (path !== state.path) {
      return;
    }
    // A save while someone else holds the lock goes to a copy at once.
    if (state.lockedBy) {
      state.unsent = bytes;
      setStatus(`Not saved: ${state.lockedBy} is editing it now.`, 'bad', [el('button', { class: 'primary', onclick: () => saveCopy(bytes) }, 'Save a copy')]);
      return;
    }
    // Saves one at a time; a save made while one is going waits, and only
    // the latest one waits.
    if (state.saving) {
      state.queued = bytes;
      return;
    }
    upload(bytes);
  }

  /** Unsaved work from Phoenix, every two minutes: kept to offer back next time. */
  function draft(bytes) {
    api('PUT', docUrl('/draft'), bytes, 'application/octet-stream').catch(() => {});
  }

  // ── A library from another cloud document ───────────────────────────────

  async function pickLibrary() {
    const results = el('div', { class: 'docs' });
    const search = el('input', { type: 'search', placeholder: 'Find a document' });
    const overlay = el('div', { class: 'cloud-overlay' }, el('div', { class: 'box' },
      el('h2', {}, 'Open a library from Phoenix Cloud'),
      el('div', { class: 'muted' }, 'Its library opens beside this document, to drag or copy symbols, pictures and sounds from.'),
      search, results,
      el('div', { class: 'row' }, el('button', { onclick: () => overlay.remove() }, 'Cancel'))));
    document.body.append(overlay);
    const show = async (q) => {
      const { json } = await api('GET', q ? `/api/documents?q=${encodeURIComponent(q)}` : '/api/documents?view=recent');
      results.replaceChildren(...(json?.documents ?? []).filter((d) => d.id !== state.id && d.canDownload && d.revision).map((d) => el('button', {
        class: 'doc',
        onclick: async () => {
          overlay.remove();
          await openLibrary(d);
        },
      }, el('span', { class: 'pic' }, d.thumbnailUrl ? el('img', { src: d.thumbnailUrl, alt: '' }) : ''), el('span', {}, d.name),
      el('span', { class: 'muted' }, d.where ?? d.owner?.name ?? ''))));
      if (!results.children.length) {
        results.append(el('div', { class: 'muted' }, q ? 'Nothing found.' : 'Search for a document by name.'));
      }
    };
    let timer = null;
    search.addEventListener('input', () => {
      clearTimeout(timer);
      timer = setTimeout(() => show(search.value.trim()), 250);
    });
    show('');
    search.focus();
  }

  async function openLibrary(doc) {
    setStatus(`Opening the library of ${doc.name}…`);
    const res = await fetch(`/api/documents/${encodeURIComponent(doc.id)}/file`, { credentials: 'same-origin' });
    if (!res.ok || !state.instance) {
      setStatus(`Could not open ${doc.name}'s library.`, 'bad');
      return;
    }
    const path = `/cloud/libraries/${doc.name.replace(/[\\/:*?"<>|\u0000-\u001f]+/g, '_').slice(0, 100) || 'Library'}.phx`;
    const { FS } = state.instance;
    FS.writeFile(path, new Uint8Array(await res.arrayBuffer()));
    FS.writeFile('/cloud/libraries/next.txt', path);
    state.instance._phoenix_open_library();
    setStatus(`Opened the library of ${doc.name}`, 'good');
  }

  // ── Events from Phoenix ──────────────────────────────────────────────────

  /** Back to where the person came from, or the document's page. */
  function leave() {
    state.leaving = true;
    if (document.referrer && new URL(document.referrer).origin === location.origin && history.length > 1) {
      history.back();
    } else {
      location.href = detailsUrl();
    }
  }

  const unsavedWork = () => Boolean(state.instance?._phoenix_has_unsaved_work?.());

  /** File > Back to Phoenix Cloud: asks about unsaved changes, then goes. */
  async function close() {
    if (unsavedWork()) {
      const choice = await ask('Save before going back?', `“${state.name}” has changes that are not saved.`,
        [['Cancel', 'cancel'], ["Don't save", 'leave'], ['Save', 'save', true]]);
      if (choice === 'cancel') {
        return;
      }
      if (choice === 'save') {
        state.instance._phoenix_save();
        while (state.saving || state.queued) {
          await new Promise((resolve) => setTimeout(resolve, 100));
        }
        // Not saved (the bar says why): stay.
        if (unsavedWork() || state.unsent) {
          return;
        }
      }
    }
    leave();
  }

  function event(name) {
    if (name === 'playerClosed') {
      leave();
    } else if (name === 'pickLibrary') {
      pickLibrary();
    } else if (name === 'close') {
      close();
    }
  }

  // Ctrl+S anywhere on the page saves to the cloud, never the browser's
  // Save Page. The page takes it before Phoenix does: Phoenix ignores keys
  // until its window has been clicked, and this way saves once either way.
  addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && !e.altKey && !e.shiftKey && e.key.toLowerCase() === 's') {
      e.preventDefault();
      e.stopImmediatePropagation();
      if (!state.play && !e.repeat) {
        state.instance?._phoenix_save?.();
      }
    }
  }, true);

  function started(instance) {
    if (!instance || state.instance) {
      return;
    }
    state.instance = instance;
    if (state.play || typeof instance._phoenix_has_unsaved_work !== 'function') {
      return;
    }
    setInterval(() => {
      const unsaved = Boolean(instance._phoenix_has_unsaved_work());
      if (unsaved !== state.unsaved) {
        state.unsaved = unsaved;
        unsavedMark.hidden = !unsaved;
      }
    }, 1000);
  }

  addEventListener('beforeunload', (event) => {
    if (!state.leaving && (state.unsent || state.saving || state.queued)) {
      event.preventDefault();
      event.returnValue = '';
    }
  });

  window.phoenixHost = {
    prepare,
    started,
    owns: (path) => Boolean(state.path) && path === state.path,
    // Leaving on purpose (File > Back to Phoenix Cloud): no "Leave site?".
    leaving: () => Boolean(state.leaving),
    save,
    draft,
    event,
  };
})();
