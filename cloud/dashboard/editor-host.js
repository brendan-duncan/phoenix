// The cloud's part of the editor page (/editor/?doc=ID, &play=1 to play,
// &tutorial=LESSON to run one of Phoenix's tutorials on it).
// Phoenix's page asks window.phoenixHost to prepare it before it starts;
// this fetches the document, puts it in Phoenix's file system and starts
// Phoenix on it, and takes Phoenix's saves of it (src/gui/web_files.h) as
// new versions of the cloud document. A bar above Phoenix shows where the
// document is and whether its last save reached the cloud.

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
  };

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
    #cloudbar .grow { flex: 1; }
    #cloudbar button, #cloudbar .button { height: 24px; padding: 0 10px; border-radius: 6px; border: 1px solid #3a3a40;
      background: #232327; color: inherit; font: inherit; cursor: pointer; display: inline-flex; align-items: center; }
    #cloudbar .primary { background: #ff7a2f; border-color: #ff7a2f; color: #1b0d04; font-weight: 600; }
    #screen { position: absolute; top: 34px; left: 0; right: 0; bottom: 0; width: auto !important; height: auto !important; }
    #loading { top: 34px !important; }
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
    node.append(...children.filter((c) => c !== null));
    return node;
  };

  const status = el('span', { class: 'status' });
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

  function showBar() {
    nameSpan.textContent = state.name;
    modeLink.textContent = state.play ? 'Edit' : 'Play';
    modeLink.href = pageUrl(!state.play);
    const bar = el('div', { id: 'cloudbar' },
      el('a', { class: 'home', href: '/', title: 'Back to your files' }, el('img', { src: '/ui/logo.png', alt: '' }), 'Phoenix Cloud'),
      el('span', { class: 'sep' }, '/'),
      el('a', { href: `/d/${encodeURIComponent(state.id)}${state.link ? `?link=${encodeURIComponent(state.link)}` : ''}`, title: 'Details and versions' }, nameSpan),
      status,
      actions,
      el('span', { class: 'grow' }),
      modeLink);
    document.body.prepend(bar);
  }

  async function api(method, url, body, type = 'application/json') {
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
    const res = await fetch(url, { method, headers, body, credentials: 'same-origin' });
    let json = null;
    if (res.headers.get('content-type')?.startsWith('application/json')) {
      json = await res.json().catch(() => null);
    }
    return { res, json };
  }

  async function prepare(config, loadingStatus) {
    const me = await api('GET', '/api/me');
    if (!me.json?.user) {
      location.href = `/signin?return=${encodeURIComponent(location.pathname + location.search)}`;
      await new Promise(() => {});
    }
    state.csrf = me.json.csrf;
    const info = await api('GET', `/api/documents/${encodeURIComponent(state.id)}`);
    if (!info.res.ok) {
      throw new Error(info.json?.message ?? `the document could not be found (${info.res.status})`);
    }
    const doc = info.json.document;
    state.name = doc.name;
    state.role = doc.role;
    state.revision = doc.revision;
    document.title = `${doc.name} · Phoenix`;
    showBar();
    if (!doc.revision) {
      throw new Error('this document has nothing saved in it yet');
    }
    if (!doc.canDownload) {
      throw new Error('its owner does not let viewers open it in the editor');
    }
    loadingStatus.textContent = `Opening ${doc.name}…`;
    const file = await fetch(`/api/documents/${encodeURIComponent(state.id)}/file`, { credentials: 'same-origin', headers: state.link ? { 'X-Phoenix-Link': state.link } : {} });
    if (!file.ok) {
      throw new Error(`the document could not be read (${file.status})`);
    }
    state.revision = Number(file.headers.get('X-Phoenix-Revision')) || doc.revision;
    const bytes = new Uint8Array(await file.arrayBuffer());
    state.path = `/cloud/${doc.name.replace(/[\\/:*?"<>|\u0000-\u001f]+/g, '_').slice(0, 100) || 'Untitled'}.phx`;
    // --hosted: the document is this page's, so Phoenix leaves out New,
    // Open, Save As and the like.
    config.arguments = [state.path, '--hosted', ...(state.play ? ['--play'] : []), ...(state.tutorial && !state.play ? ['--tutorial', state.tutorial] : [])];
    // A reload (or Play and back) does not start the lesson again.
    if (state.tutorial) {
      history.replaceState(null, '', pageUrl(state.play));
    }
    config.preRun = [...(config.preRun ?? []), (instance) => {
      try {
        instance.FS.mkdir('/cloud');
      } catch {
        // There already.
      }
      instance.FS.writeFile(state.path, bytes);
    }];
    api('POST', `/api/documents/${encodeURIComponent(state.id)}/opened`, '{}').catch(() => {});
    const editor = doc.role === 'editor' || doc.role === 'owner';
    if (state.play) {
      setStatus('Playing');
    } else {
      setStatus(editor ? `Version ${state.revision} · Save keeps a new version here` : 'You can view it; save to keep a copy in your files');
    }
  }

  async function saveCopy(bytes) {
    setStatus('Saving a copy…');
    const { res, json } = await api('POST', `/api/documents/upload?name=${encodeURIComponent(`${state.name} copy.phx`)}`, bytes, 'application/octet-stream');
    if (!res.ok) {
      state.unsent = bytes;
      setStatus(`Not saved: ${json?.message ?? res.status}`, 'bad', [el('button', { onclick: () => saveCopy(bytes) }, 'Try again')]);
      return;
    }
    state.id = json.document.id;
    state.name = json.document.name;
    state.revision = json.document.revision;
    state.role = 'owner';
    state.unsent = null;
    state.link = null;
    nameSpan.textContent = state.name;
    history.replaceState(null, '', pageUrl(state.play));
    modeLink.href = pageUrl(!state.play);
    setStatus('Saved as a copy in your files', 'good');
  }

  async function upload(bytes) {
    state.saving = true;
    setStatus('Saving to the cloud…');
    try {
      const { res, json } = await api('PUT', `/api/documents/${encodeURIComponent(state.id)}/file?base=${state.revision}`, bytes, 'application/octet-stream');
      if (res.ok) {
        state.revision = json.revision;
        state.unsent = null;
        setStatus(`Saved · version ${state.revision}`, 'good');
      } else if (res.status === 409 || res.status === 403) {
        state.unsent = bytes;
        const why = res.status === 409 ? 'Someone else saved it since you opened it.' : 'You can view this document but not save over it.';
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
    // Saves one at a time; a save made while one is going waits, and only
    // the latest one waits.
    if (state.saving) {
      state.queued = bytes;
      return;
    }
    upload(bytes);
  }

  addEventListener('beforeunload', (event) => {
    if (state.unsent || state.saving || state.queued) {
      event.preventDefault();
      event.returnValue = '';
    }
  });

  window.phoenixHost = {
    prepare,
    owns: (path) => Boolean(state.path) && path === state.path,
    save,
  };
})();
