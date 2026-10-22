// Pages other than the file views: signing in, invites, the account page,
// a group's members and settings, and the admin pages.

import { api, del, get, patch, post, send } from './api.js';
import { navigate, refreshMe, reloadShell, rerender, state } from './app.js';
import { renderListing } from './files.js';
import { adminReports, messagingPanel, newMessageDialog, openGroupChat } from './messages.js';
import {
  avatar, bytes, clear, confirmDialog, copyText, dateTime, formDialog, h, icon, person, showError, timeAgo, toast,
} from './ui.js';

const LOGIN_ERRORS = {
  registration_closed: 'New accounts are closed for now. Someone with an account can ask an admin for an invite for you.',
  invite_invalid: 'That invite has been used, has run out or was turned off.',
  invite_email: 'That invite is for another email address. Sign in with the account it was sent to.',
  email_in_use: 'Someone already signs in with that email. Sign in the way you did before, then link this one from your account page.',
  identity_taken: 'That sign-in belongs to another Phoenix Cloud account.',
  pending_email: 'Confirm your email address first: follow the link in the email we sent.',
  pending_admin: 'Your account is made. An admin has to unlock it before you can sign in.',
  suspended: 'This account is suspended.',
  state: 'That sign-in took too long or was already used. Try again.',
  cancelled: 'Signing in was cancelled.',
  failed: 'Signing in did not work. Try again.',
};

const NEW_ACCOUNT_NOTES = {
  open: null,
  email: 'We email you a link to confirm your address before you can sign in.',
  admin: 'An admin unlocks new accounts before they can sign in.',
};

function logo() {
  return h('img', { class: 'logo', src: '/ui/logo.png', alt: '' });
}

function lockText(me) {
  return me.settings.lockMessage || 'Phoenix Cloud is closed for maintenance for a while. Try again later.';
}

/** What people who are not admins see while the site is locked. */
export function renderLocked(signOut) {
  return card(logo(), h('h1', {}, 'Phoenix Cloud is closed for now'),
    h('p', { class: 'muted' }, lockText(state.me)),
    h('button', { type: 'button', class: 'btn wide', onclick: signOut }, 'Sign out'));
}

function card(...children) {
  return h('div', { class: 'center' }, h('div', { class: 'signin' }, children));
}

function providerButtons(providers, query) {
  if (!providers.length) {
    return null;
  }
  return [
    h('div', { class: 'divider' }, 'or'),
    providers.map((p) => h('a', { class: 'btn wide', href: `/api/auth/start/${p.id}?${query}` }, `Continue with ${p.name}`)),
  ];
}

function field(label, input) {
  return h('label', { class: 'field' }, h('span', {}, label), input);
}

/** A message box that a form fills in when the server says no. */
function messageBox() {
  return h('div', { class: 'error hidden', role: 'alert' });
}

function say(box, text, kind = 'error') {
  box.className = kind;
  clear(box, text);
}

export function renderSignin(params) {
  const me = state.me;
  const returnTo = params.get('return') || '/';
  const error = params.get('error');
  const login = h('input', { type: 'text', name: 'login', autocomplete: 'username', required: true, autofocus: true });
  const password = h('input', { type: 'password', name: 'password', autocomplete: 'current-password', required: true });
  const box = messageBox();
  if (error) {
    say(box, error === 'locked' ? lockText(me) : (LOGIN_ERRORS[error] ?? LOGIN_ERRORS.failed));
  } else if (me.settings.locked) {
    say(box, `${lockText(me)} Only admins can sign in until then.`, 'note');
  }
  const resend = h('button', {
    type: 'button', class: 'btn ghost hidden', onclick: async () => {
      await post('/api/auth/resend', { login: login.value.trim() }).catch(showError);
      say(box, 'If that account is waiting for its email to be confirmed, a new link is on its way.', 'note');
    },
  }, 'Send the confirmation email again');
  const form = h('form', { class: 'stack' },
    field('Name or email', login),
    field('Password', password),
    h('button', { type: 'submit', class: 'btn primary wide' }, 'Sign in'));
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await post('/api/auth/login', { login: login.value.trim(), password: password.value });
      await refreshMe();
      navigate(returnTo, { replace: true });
    } catch (err) {
      say(box, err.message);
      resend.classList.toggle('hidden', err.code !== 'pending_email');
      password.value = '';
    }
  });
  const query = new URLSearchParams({ return: returnTo });
  return card(
    logo(),
    h('h1', {}, 'Sign in to Phoenix Cloud'),
    box,
    resend,
    form,
    providerButtons(me.providers, query),
    me.settings.registration && !me.settings.locked
      ? h('p', { class: 'small' }, 'New here? ', h('a', { class: 'link', href: `/register${returnTo !== '/' ? `?return=${encodeURIComponent(returnTo)}` : ''}` }, 'Create an account'))
      : null,
    h('p', { class: 'small' }, h('a', { class: 'link', href: '/forgot' }, 'Forgot your password?')));
}

/**
 * Checks a name with the server as it is typed (after a pause), showing
 * the answer in `hint`. `onChange(ok)` hears each answer; `ok()` says
 * whether the name as it stands is free.
 */
export function nameChecker(input, hint, onChange = () => {}, { current = null } = {}) {
  let timer = null;
  let asked = 0;
  // A name that has not changed is fine as it is.
  let good = Boolean(current) && input.value.trim() === current;
  const show = (text, cls) => {
    hint.textContent = text;
    hint.className = `hint ${cls}`;
  };
  const check = async () => {
    const n = ++asked;
    const value = input.value;
    if (!value.trim()) {
      good = false;
      show('', '');
      onChange(false);
      return;
    }
    if (current && value.trim().replace(/\s+/g, ' ') === current) {
      good = true;
      show('', '');
      onChange(true);
      return;
    }
    try {
      const answer = await get(`/api/auth/name?name=${encodeURIComponent(value)}`);
      if (n !== asked) {
        return;
      }
      good = answer.available;
      show(answer.available ? `“${answer.name}” is free` : answer.problem, answer.available ? 'good' : 'bad');
    } catch {
      good = false;
      show('Could not check the name.', 'bad');
    }
    onChange(good);
    // A form that switches its button on and off checks again (ui.js formDialog).
    input.form?.dispatchEvent(new Event('recheck'));
  };
  input.addEventListener('input', () => {
    good = false;
    onChange(false);
    clearTimeout(timer);
    timer = setTimeout(check, 250);
  });
  return { ok: () => good, check };
}

export function renderRegister(params) {
  const me = state.me;
  const invite = params.get('invite');
  if ((!me.settings.registration && !invite) || me.settings.locked) {
    return card(logo(), h('h1', {}, 'New accounts are closed'),
      h('p', { class: 'muted' }, me.settings.locked ? lockText(me) : 'Phoenix Cloud is not taking new accounts for now. Someone with an account can ask an admin for an invite for you.'),
      h('a', { class: 'btn wide', href: '/signin' }, 'Sign in'));
  }
  const name = h('input', { type: 'text', name: 'name', autocomplete: 'username', required: true, maxlength: 30, autofocus: true });
  const email = h('input', { type: 'email', name: 'email', autocomplete: 'email', required: true });
  const password = h('input', { type: 'password', name: 'password', autocomplete: 'new-password', required: true, minlength: 8 });
  const again = h('input', { type: 'password', name: 'again', autocomplete: 'new-password', required: true, minlength: 8 });
  const nameHint = h('div', { class: 'hint' });
  const passwordHint = h('div', { class: 'hint' });
  const box = messageBox();
  const note = invite ? 'Your invite lets you in as soon as the account is made.' : NEW_ACCOUNT_NOTES[me.settings.newAccounts];
  // While an admin unlocks each new account, the request comes with a word about who is asking.
  const asking = !invite && me.settings.newAccounts === 'admin';
  const message = asking ? h('textarea', { name: 'message', rows: 4, maxlength: 1000, required: true, placeholder: 'Who you are, and what you would like to make with Phoenix' }) : null;
  const submit = h('button', { type: 'submit', class: 'btn primary wide', disabled: true }, 'Create account');

  // The button is on only when every field is right.
  const passwordsOk = () => {
    const long = password.value.length >= 8;
    const same = password.value === again.value;
    if (!password.value) {
      passwordHint.textContent = '';
    } else if (!long) {
      passwordHint.textContent = 'At least 8 characters.';
    } else if (again.value && !same) {
      passwordHint.textContent = 'The two passwords are not the same.';
    } else {
      passwordHint.textContent = '';
    }
    passwordHint.className = `hint ${passwordHint.textContent ? 'bad' : ''}`;
    return long && Boolean(again.value) && same;
  };
  let checker;
  const update = () => {
    submit.disabled = !(checker?.ok() && email.checkValidity() && email.value.trim() && passwordsOk() && (!message || message.value.trim()));
  };
  checker = nameChecker(name, nameHint, update);
  for (const input of [email, password, again, message].filter(Boolean)) {
    input.addEventListener('input', update);
  }

  const form = h('form', { class: 'stack' },
    h('label', { class: 'field' }, h('span', {}, 'Name'), name, nameHint,
      h('span', { class: 'muted small' }, 'What others see, and what you sign in with.')),
    field('Email', email),
    field('Password (at least 8 characters)', password),
    h('label', { class: 'field' }, h('span', {}, 'Password again'), again, passwordHint),
    asking ? h('div', { class: 'note' }, 'Phoenix Cloud is in closed beta: an admin turns on each new account by hand. Introduce yourself in a line or two, so we can tell you are a real person.') : null,
    asking ? h('label', { class: 'field' }, h('span', {}, 'Request message'), message) : null,
    note && !asking ? h('p', { class: 'small muted' }, note) : null,
    submit);
  const holder = h('div', { class: 'stack' }, box, form);
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    if (submit.disabled) {
      return;
    }
    submit.disabled = true;
    try {
      const made = await post('/api/auth/register', { name: name.value, email: email.value.trim(), password: password.value, message: message?.value.trim(), invite });
      if (made.status === 'active') {
        await refreshMe();
        navigate(params.get('return') || '/', { replace: true });
        return;
      }
      clear(holder, h('div', { class: 'note' }, made.status === 'pending_email'
        ? `Your account is made. We sent a link to ${email.value.trim()}: follow it to confirm your address, then sign in.`
        : 'Your account is made. An admin unlocks new accounts; you can sign in once yours is unlocked.'),
      h('a', { class: 'btn wide', href: '/signin' }, 'Back to sign in'));
    } catch (err) {
      say(box, err.message);
      update();
    }
  });
  return card(
    logo(),
    h('h1', {}, invite ? 'You are invited' : 'Create your account'),
    holder,
    providerButtons(me.providers, new URLSearchParams({ return: '/', ...(invite ? { invite } : {}) })),
    h('p', { class: 'small' }, 'Have an account? ', h('a', { class: 'link', href: '/signin' }, 'Sign in')));
}

export function renderForgot() {
  const email = h('input', { type: 'email', name: 'email', autocomplete: 'email', required: true, autofocus: true });
  const body = h('div', { class: 'stack' });
  const form = h('form', { class: 'stack' },
    h('p', { class: 'muted flush' }, 'We email you a link to choose a new password.'),
    field('Email', email),
    h('button', { type: 'submit', class: 'btn primary wide' }, 'Send the link'));
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await post('/api/auth/forgot', { email: email.value.trim() });
      clear(body, h('div', { class: 'note' }, `If ${email.value.trim()} has an account, a link to choose a new password is on its way. It works for 2 hours.`),
        h('a', { class: 'btn wide', href: '/signin' }, 'Back to sign in'));
    } catch (err) {
      showError(err);
    }
  });
  clear(body, form);
  return card(logo(), h('h1', {}, 'Forgot your password?'), body,
    h('p', { class: 'small' }, h('a', { class: 'link', href: '/signin' }, 'Back to sign in')));
}

export function renderReset(token) {
  const password = h('input', { type: 'password', autocomplete: 'new-password', required: true, minlength: 8, autofocus: true });
  const again = h('input', { type: 'password', autocomplete: 'new-password', required: true, minlength: 8 });
  const submit = h('button', { type: 'submit', class: 'btn primary wide', disabled: true }, 'Set password');
  const hint = h('div', { class: 'hint' });
  const box = messageBox();
  const check = () => {
    const ok = password.value.length >= 8 && password.value === again.value;
    hint.textContent = again.value && password.value !== again.value ? 'The two passwords are not the same.' : '';
    hint.className = `hint ${hint.textContent ? 'bad' : ''}`;
    submit.disabled = !ok;
  };
  password.addEventListener('input', check);
  again.addEventListener('input', check);
  const form = h('form', { class: 'stack' },
    field('New password (at least 8 characters)', password),
    h('label', { class: 'field' }, h('span', {}, 'New password again'), again, hint),
    submit);
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await post('/api/auth/reset', { token, password: password.value });
      await refreshMe();
      navigate('/', { replace: true });
    } catch (err) {
      say(box, err.message);
    }
  });
  return card(logo(), h('h1', {}, 'Choose a new password'), box, form);
}

// ── The player ────────────────────────────────────────────────────────────

/**
 * A document played full size: its picture with a play button first (so it
 * starts with sound, and loads only when wanted), then Phoenix's player
 * for people who may open it, or an HTML5 version for people who may not.
 */
export function renderPlayer(id) {
  const stage = h('div', { class: 'player-stage' }, h('div', { class: 'muted' }, 'Loading…'));
  const title = h('span', { class: 'player-title' }, '');
  const back = h('a', { class: 'btn ghost', href: `/d/${encodeURIComponent(id)}` }, icon('x'), 'Close');
  const fill = h('button', {
    type: 'button', class: 'btn ghost', onclick: () => (document.fullscreenElement ? document.exitFullscreen() : stage.requestFullscreen()).catch(() => {}),
  }, 'Full screen');
  const edit = h('a', { class: 'btn ghost hidden', href: '#' }, icon('edit'), 'Edit');
  const page = h('div', { class: 'player' },
    h('div', { class: 'player-bar' }, h('a', { class: 'brand', href: '/' }, h('img', { src: '/ui/logo.png', alt: '' })), title, h('div', { class: 'grow' }), edit, fill, back),
    stage);
  get(`/api/documents/${encodeURIComponent(id)}`).then(({ document: doc }) => {
    document.title = `${doc.name} · Phoenix Cloud`;
    title.textContent = doc.name;
    if (doc.canOpen && state.me.editor) {
      edit.href = `/editor/?doc=${encodeURIComponent(doc.id)}`;
      edit.classList.remove('hidden');
    }
    const start = h('button', { type: 'button', class: 'player-start', 'aria-label': `Play ${doc.name}` },
      doc.thumbnailUrl ? h('img', { src: doc.thumbnailUrl, alt: '' }) : null,
      h('span', { class: 'player-button' }, icon('play')));
    start.addEventListener('click', async () => {
      clear(stage, h('div', { class: 'muted' }, 'Getting it ready…'));
      try {
        const play = await get(`/api/documents/${encodeURIComponent(doc.id)}/play`);
        if (play.mode === 'editor') {
          location.href = play.url;
          return;
        }
        const frame = h('iframe', { src: play.url, title: doc.name, allow: 'autoplay; fullscreen', sandbox: 'allow-scripts allow-same-origin' });
        if (play.width && play.height) {
          frame.style.aspectRatio = `${play.width} / ${play.height}`;
        }
        clear(stage, frame);
      } catch (err) {
        clear(stage, h('div', { class: 'status-bad' }, err.message));
      }
    });
    clear(stage, start);
  }).catch((err) => clear(stage, h('div', { class: 'status-bad' }, err.message)));
  return page;
}

export function renderConfirm(token) {
  const body = h('div', { class: 'stack' }, h('p', {}, 'Confirming your email address…'));
  post('/api/auth/confirm', { token }).then(async (result) => {
    if (result.status === 'active') {
      await refreshMe();
      clear(body, h('p', {}, 'Your email address is confirmed.'), h('a', { class: 'btn primary wide', href: '/' }, 'Go to your files'));
    } else {
      clear(body, h('p', {}, `Your email address is confirmed. ${result.message ?? ''}`), h('a', { class: 'btn wide', href: '/signin' }, 'Back to sign in'));
    }
  }).catch((err) => {
    clear(body, h('div', { class: 'error' }, err.message), h('a', { class: 'btn wide', href: '/signin' }, 'Back to sign in'));
  });
  return card(logo(), h('h1', {}, 'Phoenix Cloud'), body);
}

export function renderInvite(token) {
  const body = h('div', { class: 'stack' }, h('p', {}, 'Checking your invite…'));
  get(`/api/invites/${encodeURIComponent(token)}`).then((invite) => {
    const me = state.me;
    if (!invite.valid) {
      clear(body, h('p', {}, LOGIN_ERRORS.invite_invalid), h('a', { class: 'btn wide', href: '/signin' }, 'Sign in'));
      return;
    }
    if (me.user) {
      clear(body, h('p', {}, `You are signed in as ${me.user.name}. The invite is for someone new.`), h('a', { class: 'btn primary wide', href: '/' }, 'Go to your files'));
      return;
    }
    navigate(`/register?invite=${encodeURIComponent(token)}`, { replace: true });
  }).catch(showError);
  return card(logo(), h('h1', {}, 'You are invited'), body);
}

// ── Account ───────────────────────────────────────────────────────────────

/** The profile picture, with ways to change or remove it. */
function pictureRow(me) {
  const input = h('input', { type: 'file', accept: 'image/png,image/jpeg,image/webp,image/gif', class: 'hidden' });
  input.addEventListener('change', async () => {
    const file = input.files[0];
    if (!file) {
      return;
    }
    try {
      await send('PUT', '/api/me/avatar', file);
      await reloadShell();
      toast('Picture changed.');
    } catch (err) {
      showError(err);
    }
  });
  return h('div', { class: 'row' }, avatar(me, 'large'),
    h('button', { type: 'button', class: 'btn', onclick: () => input.click() }, 'Change picture'),
    me.avatarUrl ? h('button', {
      type: 'button', class: 'btn ghost', onclick: async () => {
        await del('/api/me/avatar').catch(showError);
        await reloadShell();
      },
    }, 'Remove') : null,
    input);
}

export async function renderAccount(target) {
  const [{ sessions }, { identities }] = await Promise.all([get('/api/me/sessions'), get('/api/me/identities')]);
  if (!target.isConnected) {
    return;
  }
  const me = state.me.user;
  const params = new URLSearchParams(location.search);
  const name = h('input', { type: 'text', name: 'name', value: me.name, maxlength: 30, required: true, autocomplete: 'username' });
  const nameHint = h('div', { class: 'hint' });
  const email = h('input', { type: 'email', name: 'email', value: me.email ?? '', autocomplete: 'email' });
  const bio = h('textarea', { name: 'bio', rows: 3, maxlength: 500, value: me.bio });
  const save = h('button', { type: 'submit', class: 'btn primary' }, 'Save');
  const names = nameChecker(name, nameHint, (ok) => {
    save.disabled = !ok;
  }, { current: me.name });
  const profile = h('form', { class: 'panel stack cards-form' },
    h('h2', {}, 'Profile'),
    pictureRow(me),
    h('label', { class: 'field' }, h('span', {}, 'Name'), name, nameHint,
      h('span', { class: 'muted small' }, 'What others see, and what you sign in with.')),
    h('label', { class: 'field' }, h('span', {}, 'Email'), email),
    h('label', { class: 'field' }, h('span', {}, 'About you'), bio),
    h('div', {}, save));
  profile.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      if (!names.ok()) {
        return;
      }
      await patch('/api/me', { name: name.value, email: email.value.trim() || undefined, bio: bio.value });
      await reloadShell();
      toast('Saved.');
    } catch (err) {
      showError(err);
    }
  });

  const current = h('input', { type: 'password', autocomplete: 'current-password', required: me.hasPassword });
  const fresh = h('input', { type: 'password', autocomplete: 'new-password', required: true, minlength: 8 });
  const again = h('input', { type: 'password', autocomplete: 'new-password', required: true, minlength: 8 });
  const passwordForm = h('form', { class: 'panel stack cards-form', id: 'password' },
    h('h2', {}, 'Password'),
    me.passwordIsDefault ? h('div', { class: 'status-bad' }, 'You still have the password this server started with. Choose your own.') : null,
    me.hasPassword ? h('label', { class: 'field' }, h('span', {}, 'Current password'), current) : null,
    h('label', { class: 'field' }, h('span', {}, 'New password (at least 8 characters)'), fresh),
    h('label', { class: 'field' }, h('span', {}, 'New password again'), again),
    h('div', {}, h('button', { type: 'submit', class: 'btn primary' }, 'Change password')),
    h('p', { class: 'muted small flush' }, 'Changing it signs you out everywhere else.'));
  passwordForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    if (fresh.value !== again.value) {
      showError(new Error('The two new passwords are not the same.'));
      return;
    }
    try {
      await patch('/api/me/password', { current: current.value, password: fresh.value });
      await reloadShell();
      toast('Password changed.');
    } catch (err) {
      showError(err);
    }
  });

  const providerName = (id) => state.me.providers.find((p) => p.id === id)?.name ?? id;
  const linked = new Set(identities.map((i) => i.provider));
  const signins = state.me.providers.length || identities.length ? h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Other ways to sign in'),
    params.get('error') ? h('div', { class: 'status-bad' }, LOGIN_ERRORS[params.get('error')] ?? LOGIN_ERRORS.failed) : null,
    identities.map((i) => h('div', { class: 'row' },
      h('div', { class: 'grow' }, h('div', {}, providerName(i.provider)), h('div', { class: 'muted small' }, `${i.email ?? ''} · last used ${timeAgo(i.lastUsedAt)}`)),
      h('button', {
        type: 'button', class: 'btn ghost', onclick: async () => {
          try {
            await del(`/api/me/identities/${encodeURIComponent(i.provider)}/${encodeURIComponent(i.subject)}`);
            rerender();
          } catch (err) {
            showError(err);
          }
        },
      }, 'Remove'))),
    state.me.providers.filter((p) => !linked.has(p.id)).map((p) => h('div', {}, h('a', { class: 'btn', href: `/api/auth/start/${p.id}?link=1` }, `Link ${p.name}`)))) : null;

  const sessionList = h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Where you are signed in'),
    sessions.map((s) => h('div', { class: 'row' },
      h('div', { class: 'grow' }, h('div', {}, s.userAgent || 'Unknown browser'), h('div', { class: 'muted small' }, `${s.current ? 'This browser · ' : ''}active ${timeAgo(s.lastSeenAt)} · signed in ${dateTime(s.createdAt)}`)),
      s.current ? null : h('button', { type: 'button', class: 'btn ghost', onclick: async () => { await del(`/api/me/sessions/${s.id}`).catch(showError); rerender(); } }, 'Sign out'))));

  const plan = state.me.plan;
  const planPanel = h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Plan'),
    h('div', { class: 'row' }, h('strong', {}, plan.label),
      plan.team ? h('span', { class: 'muted small' }, plan.managed ? `Your account is managed by ${plan.team.name}.` : `From your team, ${plan.team.name}.`) : null),
    plan.id === 'beta' ? h('p', { class: 'muted small flush' }, 'Free while Phoenix Cloud is in beta.') : null);

  const leaving = h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Delete your account'),
    h('p', { class: 'muted small flush' }, 'Your documents and folders are deleted with it. Before it goes, you can give the documents others use, and the groups you own, to someone else.'),
    h('div', {}, h('button', { type: 'button', class: 'btn danger', onclick: () => deleteAccountDialog().catch(showError) }, icon('trash'), 'Delete account…')));

  const notices = h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Notifications'),
    h('p', { class: 'muted small flush' }, 'Choose what Phoenix Cloud tells you about, here and by email.'),
    h('div', {}, h('a', { class: 'btn', href: '/notifications#settings' }, icon('bell'), 'Notification settings')));
  const messaging = await messagingPanel();
  if (!target.isConnected) {
    return;
  }
  clear(target, h('div', { class: 'page-head' }, h('h1', {}, 'Account')),
    h('div', { class: 'stack' }, profile, planPanel, notices, messaging, passwordForm, signins, sessionList, leaving));
  if (location.hash === '#password') {
    passwordForm.scrollIntoView();
    (me.hasPassword ? current : fresh).focus();
  }
}

/**
 * Deleting one's own account: who takes the documents others use and the
 * groups one owns, then the password (or name) to be sure.
 */
async function deleteAccountDialog() {
  const plan = await get('/api/me/deletion');
  if (plan.managedBy) {
    showError(new Error(`${plan.managedBy.name} manages your account. Ask its owner to remove it.`));
    return;
  }
  if (plan.lastAdmin) {
    showError(new Error('You are the only admin of Phoenix Cloud. Make someone else an admin first.'));
    return;
  }
  const choose = (name, label, candidates, required) => h('label', { class: 'field' }, h('span', {}, label),
    h('select', { name, required },
      required ? null : h('option', { value: '' }, 'Delete it'),
      candidates.map((c) => h('option', { value: c.id }, `Give it to ${c.name}`))));
  const fields = [
    h('p', { class: 'flush' }, `This deletes your account and ${plan.documentCount === 1 ? 'your 1 document' : `your ${plan.documentCount} documents`} (${bytes(plan.bytes)}), with your folders. It cannot be undone.`),
    plan.documents.length ? [h('h3', {}, 'Documents others use'), plan.documents.map((d) => choose(`doc:${d.id}`, d.name, d.candidates, false))] : null,
    plan.groups.length ? [h('h3', {}, 'Groups you own'),
      plan.groups.map((g) => (g.candidates.length
        ? choose(`group:${g.id}`, `${g.name} (it needs a new owner)`, g.candidates, true)
        : h('div', { class: 'muted small' }, `${g.name} has no one else in it, so it is deleted with its files.`)))] : null,
    plan.needsPassword
      ? h('label', { class: 'field' }, h('span', {}, 'Your password'), h('input', { type: 'password', name: 'password', required: true, autocomplete: 'current-password' }))
      : h('label', { class: 'field' }, h('span', {}, `Type your name, ${state.me.user.name}, to confirm`), h('input', { type: 'text', name: 'name', required: true })),
  ];
  const done = await formDialog({
    title: 'Delete your account?',
    submit: 'Delete my account',
    danger: true,
    fields,
    onSubmit: (data) => {
      const documents = {};
      const groups = {};
      for (const [key, value] of Object.entries(data)) {
        if (key.startsWith('doc:') && value) {
          documents[key.slice(4)] = value;
        } else if (key.startsWith('group:') && value) {
          groups[key.slice(6)] = value;
        }
      }
      return api('DELETE', '/api/me', { password: data.password, name: data.name, documents, groups });
    },
  });
  if (done) {
    await refreshMe();
    navigate('/signin', { replace: true });
  }
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
  const who = h('input', { type: 'text', placeholder: 'Name or email', 'aria-label': 'Who', required: true });
  const memberRole = h('select', { 'aria-label': 'Role' }, h('option', { value: 'member' }, 'Member'), h('option', { value: 'admin' }, 'Admin'));
  const add = h('form', { class: 'row cards-form' }, who, memberRole, h('button', { type: 'submit', class: 'btn primary' }, icon('plus'), 'Add'));
  add.addEventListener('submit', (e) => {
    e.preventDefault();
    act(() => post(`/api/groups/${group.id}/members`, { who: who.value.trim(), role: memberRole.value }))();
  });
  const rows = members.map((m) => {
    const owner = m.user.id === group.ownerId;
    const roleBox = owner ? h('span', { class: 'badge accent' }, 'Owner') : h('select', { 'aria-label': 'Role', disabled: !admin },
      h('option', { value: 'member', selected: m.role === 'member' }, 'Member'), h('option', { value: 'admin', selected: m.role === 'admin' }, 'Admin'));
    if (!owner) {
      roleBox.addEventListener('change', act(() => patch(`/api/groups/${group.id}/members/${m.user.id}`, { role: roleBox.value })));
    }
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
    const remove = owner ? null : admin || self ? h('button', {
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
    h('p', { class: 'muted small' }, 'Admins add and remove members and choose what members can do with the group\'s files. The owner is always an admin, and is the one who can hand the group on or delete it.'),
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
  const mine = group.ownerId === state.me.user.id;
  const others = detail.members.filter((m) => m.user.id !== group.ownerId);
  const handOn = mine ? h('div', { class: 'panel stack cards-form' },
    h('h2', {}, 'Owner'),
    h('p', { class: 'muted flush' }, 'You own this group. Hand it to another member to make them its owner; you stay an admin.'),
    h('div', {}, h('button', {
      type: 'button', class: 'btn', disabled: !others.length, onclick: async () => {
        const result = await formDialog({
          title: `Hand ${group.name} on`,
          submit: 'Make them the owner',
          fields: h('label', { class: 'field' }, h('span', {}, 'New owner'),
            h('select', { name: 'to', required: true }, others.map((m) => h('option', { value: m.user.id }, m.user.name)))),
          onSubmit: (data) => post(`/api/groups/${group.id}/transfer`, { to: data.to }),
        });
        if (result) {
          toast('Handed on.');
          rerender();
        }
      },
    }, icon('users'), 'Transfer ownership…'))) : null;
  const danger = mine ? h('div', { class: 'panel stack cards-form' },
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
    }, icon('trash'), 'Delete group'))) : null;
  return [form, handOn, danger];
}

export async function renderGroupPage(target, id, tab, folder) {
  const detail = await get(`/api/groups/${id}`);
  if (!target.isConnected) {
    return;
  }
  const { group, role } = detail;
  const head = h('div', { class: 'page-head' }, h('span', { class: 'avatar' }, icon('users')), h('div', { class: 'grow' },
    h('h1', {}, group.name), h('div', { class: 'muted small' }, `@${group.handle} · ${detail.members.length} member${detail.members.length === 1 ? '' : 's'}${role === 'admin' ? ' · you are an admin' : ''}`)),
    role ? h('button', { type: 'button', class: 'btn', onclick: () => openGroupChat(group.id) }, icon('chat'), 'Chat') : null);
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

const STATUS_LABELS = {
  active: 'Active',
  pending_email: 'Waiting for email confirmation',
  pending_admin: 'Waiting to be unlocked',
  suspended: 'Suspended',
};

function adminTabs(tab, waiting) {
  return h('div', { class: 'tabs' }, [['users', waiting ? `Accounts (${waiting} waiting)` : 'Accounts'], ['invites', 'Invites'], ['settings', 'Settings'], ['reports', 'Reports'], ['fonts', 'Fonts'], ['email', 'Email'], ['audit', 'Activity']]
    .map(([t, label]) => h('a', { href: `/admin/${t}`, class: tab === t ? 'active' : null }, label)));
}

/** Runs an admin change, then shows the page again (errors as a toast). */
function change(fn) {
  return async () => {
    try {
      if ((await fn()) !== false) {
        rerender();
      }
    } catch (err) {
      showError(err);
    }
  };
}

const setStatus = (u, status) => change(() => patch(`/api/admin/users/${u.id}`, { status }));

const deleteAccount = (u) => change(async () => {
  if (!(await confirmDialog({ title: `Delete ${u.name}?`, message: `The account and its ${count(u.documents, 'document')} are deleted for good.`, confirm: 'Delete account', danger: true }))) {
    return false;
  }
  await del(`/api/admin/users/${u.id}`);
  toast(`Deleted ${u.name}.`);
  return true;
});

const setPassword = (u) => change(async () => {
  const done = await formDialog({
    title: `New password for ${u.name}`,
    submit: 'Set password',
    note: 'They are signed out everywhere and sign in with this password. Send it to them yourself.',
    fields: h('label', { class: 'field' }, h('span', {}, 'Password (at least 8 characters)'), h('input', { type: 'text', name: 'password', required: true, minlength: 8, autofocus: true, autocomplete: 'off' })),
    onSubmit: (data) => patch(`/api/admin/users/${u.id}`, { password: data.password }),
  });
  if (done) {
    toast(`Set a new password for ${u.name}.`);
  }
  return Boolean(done);
});

const setQuota = (u) => change(async () => Boolean(await formDialog({
  title: `Storage for ${u.name}`,
  fields: h('label', { class: 'field' }, h('span', {}, 'Quota in MB (empty for the default)'), h('input', { type: 'number', name: 'quota', min: 0, value: u.quotaMB ?? '' })),
  onSubmit: (data) => patch(`/api/admin/users/${u.id}`, { quotaMB: data.quota === '' ? null : Number(data.quota) }),
})));

const createAccount = change(async () => {
  const name = h('input', { type: 'text', name: 'name', required: true, maxlength: 30, autofocus: true, autocomplete: 'off' });
  const hint = h('div', { class: 'hint' });
  const names = nameChecker(name, hint);
  return Boolean(await formDialog({
    title: 'Create an account',
    submit: 'Create account',
    note: 'The account is ready to use at once. Send them their name and password yourself.',
    fields: [
      h('label', { class: 'field' }, h('span', {}, 'Name (what others see, and what they sign in with)'), name, hint),
      h('label', { class: 'field' }, h('span', {}, 'Email (optional)'), h('input', { type: 'email', name: 'email' })),
      h('label', { class: 'field' }, h('span', {}, 'Password (at least 8 characters)'), h('input', { type: 'text', name: 'password', required: true, minlength: 8, autocomplete: 'off' })),
      h('label', { class: 'check' }, h('input', { type: 'checkbox', name: 'isAdmin' }), 'Make them an admin'),
    ],
    valid: (form) => names.ok() && form.elements.email.checkValidity() && form.elements.password.value.length >= 8,
    onSubmit: (data) => post('/api/admin/users', { name: data.name, email: data.email.trim() || null, password: data.password, isAdmin: data.isAdmin }),
  }));
});

function accountButtons(u, me) {
  if (u.id === me.id) {
    return [h('span', { class: 'muted small' }, 'You'), h('a', { class: 'btn ghost small-btn', href: '/account#password' }, 'Password…')];
  }
  const button = (label, onclick, cls = '') => h('button', { type: 'button', class: `btn ghost small-btn ${cls}`.trim(), onclick }, label);
  return [
    u.status.startsWith('pending') ? button('Unlock', setStatus(u, 'active'), 'accent') : null,
    u.status === 'active' ? button('Suspend', setStatus(u, 'suspended')) : null,
    u.status === 'suspended' ? button('Unsuspend', setStatus(u, 'active')) : null,
    button(u.isAdmin ? 'Remove admin' : 'Make admin', change(() => patch(`/api/admin/users/${u.id}`, { isAdmin: !u.isAdmin }))),
    button('Password…', setPassword(u)),
    button('Storage…', setQuota(u)),
    button('Delete', deleteAccount(u), 'danger'),
  ];
}

async function adminUsers() {
  const [{ users }, stats, { plans }] = await Promise.all([get('/api/admin/users'), get('/api/admin/stats'), get('/api/admin/plans')]);
  const planBox = (u) => {
    const box = h('select', { 'aria-label': `${u.name}'s plan` },
      plans.filter((p) => p.offered || p.id === u.plan).map((p) => h('option', { value: p.id, selected: p.id === u.plan }, p.label)));
    box.addEventListener('change', change(() => patch(`/api/admin/users/${u.id}`, { plan: box.value })));
    const from = u.effectivePlan.team ? h('div', { class: 'muted small' }, `${u.effectivePlan.plan} from ${u.effectivePlan.team.name}`) : null;
    return [box, from];
  };
  const me = state.me.user;
  const waiting = users.filter((u) => u.status.startsWith('pending'));
  const search = h('input', { type: 'search', placeholder: 'Find an account', 'aria-label': 'Find an account' });
  const rows = users.map((u) => {
    const row = h('tr', { dataset: { find: `${u.name} ${u.email ?? ''}`.toLowerCase() } },
      h('td', {}, h('a', { class: 'plain-link', href: `/admin/users/${u.id}`, title: 'Their account and documents' }, person(u, u.isAdmin ? 'admin' : null))),
      h('td', {}, u.email ?? '', u.email && !u.emailVerified ? h('div', { class: 'muted small' }, 'not confirmed') : null),
      h('td', { class: u.status === 'active' ? 'status-ok' : 'status-bad' }, STATUS_LABELS[u.status] ?? u.status),
      h('td', {}, planBox(u)),
      h('td', {}, String(u.documents)),
      h('td', {}, `${bytes(u.bytes)}${u.quotaMB !== null ? ` of ${u.quotaMB} MB` : ''}`),
      h('td', {}, u.lastSeenAt ? timeAgo(u.lastSeenAt) : 'never'),
      h('td', {}, h('div', { class: 'row wrap' }, accountButtons(u, me))));
    return row;
  });
  search.addEventListener('input', () => {
    const q = search.value.trim().toLowerCase();
    for (const row of rows) {
      row.classList.toggle('hidden', Boolean(q) && !row.dataset.find.includes(q));
    }
  });
  return [
    h('p', { class: 'muted' }, `${count(stats.users, 'account')} · ${count(stats.groups, 'group')} · ${count(stats.documents, 'document')} (${stats.trashed} in the trash) · ${count(stats.revisions, 'version')} · ${bytes(stats.blobs.bytes)} stored · database ${bytes(stats.databaseBytes)}`,
      stats.converter ? '' : ' · no Phoenix on the server, so new documents and FLA, SWF, SVG, EPS and AI uploads are off'),
    waiting.length ? h('div', { class: 'panel stack' },
      h('h2', {}, `Waiting to be unlocked (${waiting.length})`),
      waiting.map((u) => h('div', { class: 'row wrap waiting' },
        h('div', { class: 'grow' }, person(u, `${u.email ?? 'no email'} · ${STATUS_LABELS[u.status]} · registered ${timeAgo(u.createdAt)}`),
          u.requestMessage ? h('blockquote', { class: 'request' }, u.requestMessage) : h('div', { class: 'muted small request-none' }, 'No request message.')),
        h('button', { type: 'button', class: 'btn primary', onclick: setStatus(u, 'active') }, 'Unlock'),
        h('button', { type: 'button', class: 'btn danger', onclick: deleteAccount(u) }, 'Delete')))) : null,
    h('div', { class: 'row wrap admin-tools' }, search, h('div', { class: 'grow' }), h('button', { type: 'button', class: 'btn primary', onclick: createAccount }, icon('plus'), 'Create account')),
    h('div', { class: 'table-wrap' }, h('table', { class: 'table' },
      h('thead', {}, h('tr', {}, ['Account', 'Email', 'Status', 'Plan', 'Files', 'Storage', 'Last seen', ''].map((t) => h('th', {}, t)))),
      h('tbody', {}, rows))),
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
    h('p', { class: 'muted small flush' }, 'Someone who registers from an invite link can sign in straight away, whatever the setting for new accounts.'),
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
      form.parentNode.querySelector('.invite-table')?.replaceWith(inviteTable((await get('/api/admin/invites')).invites));
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
      h('td', { class: i.status === 'pending' ? '' : 'muted' }, i.status === 'claimed' && i.claimedBy ? `claimed by ${i.claimedBy.name}` : i.status),
      h('td', {}, i.status === 'pending' ? h('button', { type: 'button', class: 'btn ghost', onclick: change(() => del(`/api/admin/invites/${i.id}`)) }, 'Turn off') : null))))));
}

async function adminSettings() {
  const [{ settings }, { plans }] = await Promise.all([get('/api/admin/settings'), get('/api/admin/plans')]);
  const defaultPlan = h('select', {}, plans.filter((p) => p.offered).map((p) => h('option', { value: p.id, selected: p.id === settings.defaultPlan }, p.label)));
  const choice = (value, title, text) => h('label', { class: 'choice' },
    h('input', { type: 'radio', name: 'newAccounts', value, checked: settings.newAccounts === value }),
    h('div', {}, h('div', { class: 'choice-title' }, title), h('div', { class: 'muted small' }, text)));
  const beta = h('input', { type: 'checkbox', checked: settings.beta });
  const registration = h('input', { type: 'checkbox', checked: settings.registration });
  const locked = h('input', { type: 'checkbox', checked: settings.locked });
  const lockMessage = h('textarea', { rows: 2, maxlength: 500, placeholder: 'Phoenix Cloud is closed for maintenance for a while. Try again later.' }, settings.lockMessage);
  const quota = h('input', { type: 'number', min: 1, value: settings.defaultQuotaMB });
  const banner = h('input', { type: 'text', value: settings.banner, maxlength: 300, placeholder: 'A message at the top of every page' });
  const form = h('form', { class: 'panel stack cards-form' },
    h('h2', {}, 'New accounts'),
    h('label', { class: 'check' }, registration, 'People can register (off: only invites and accounts you make)'),
    h('p', { class: 'muted small flush' }, 'What an account is when someone registers. People with an invite link, and accounts an admin makes, are ready at once.'),
    choice('admin', 'Locked until an admin unlocks it', 'You see new accounts under Accounts and unlock the ones you want.'),
    choice('email', 'Locked until they confirm their email address', 'They get a link by email. (There is no mail server yet: the emails wait under Email.)'),
    choice('open', 'Open', 'Anyone who registers can sign in straight away.'),
    h('label', { class: 'field' }, h('span', {}, 'Plan new accounts start on'), defaultPlan),
    h('h2', {}, 'Sharing'),
    h('label', { class: 'check' }, beta, 'Public and link-shared documents only for people who are signed in; ask search engines not to index'),
    h('label', { class: 'field' }, h('span', {}, 'Storage for each account and group, in MB'), quota),
    h('label', { class: 'field' }, h('span', {}, 'Banner'), banner),
    h('h2', {}, 'Lock the site'),
    h('label', { class: 'check' }, locked, 'Locked: only admins can sign in and use it (for maintenance, say); everyone else sees the message below'),
    h('label', { class: 'field' }, h('span', {}, 'Message while locked'), lockMessage),
    h('div', {}, h('button', { type: 'submit', class: 'btn primary' }, 'Save')));
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      const newAccounts = form.querySelector('input[name="newAccounts"]:checked')?.value ?? 'admin';
      if (locked.checked && !settings.locked && !(await confirmDialog({
        title: 'Lock Phoenix Cloud?', message: 'Everyone but admins is signed out of using it until you unlock it here.', confirm: 'Lock it', danger: true,
      }))) {
        return;
      }
      await patch('/api/admin/settings', {
        newAccounts, registration: registration.checked, defaultPlan: defaultPlan.value, beta: beta.checked, defaultQuotaMB: Number(quota.value), banner: banner.value,
        locked: locked.checked, lockMessage: lockMessage.value.trim(),
      });
      await reloadShell();
      toast('Saved.');
    } catch (err) {
      showError(err);
    }
  });
  return [form];
}

async function adminFonts() {
  const { fonts } = await get('/api/admin/fonts');
  const input = h('input', { type: 'file', accept: '.ttf,.otf', multiple: true, class: 'hidden' });
  input.addEventListener('change', async () => {
    for (const file of input.files) {
      await send('POST', `/api/admin/fonts?name=${encodeURIComponent(file.name)}`, file).catch((err) => showError(new Error(`${file.name}: ${err.message}`)));
    }
    rerender();
  });
  return [
    h('p', { class: 'muted' }, 'The fonts the web editor loads and Phoenix on the server draws with. Liberation Sans, Serif and Mono, Carlito and Caladea stand in for Arial, Times New Roman, Courier New, Calibri and Cambria with the same widths. Run node cloud/server.mjs fonts to get them; add any .ttf or .otf you have the right to share.'),
    h('div', { class: 'row admin-tools' }, h('button', { type: 'button', class: 'btn primary', onclick: () => input.click() }, icon('upload'), 'Add fonts'), input,
      h('span', { class: 'muted small' }, `${count(fonts.length, 'font')}, ${bytes(fonts.reduce((n, f) => n + f.size, 0))} for the editor to load`)),
    fonts.length ? h('div', { class: 'table-wrap' }, h('table', { class: 'table' },
      h('thead', {}, h('tr', {}, ['Font', 'Size', ''].map((t) => h('th', {}, t)))),
      h('tbody', {}, fonts.map((f) => h('tr', {}, h('td', {}, f.name), h('td', {}, bytes(f.size)),
        h('td', {}, h('button', { type: 'button', class: 'btn ghost small-btn danger', onclick: change(() => del(`/api/admin/fonts/${encodeURIComponent(f.name)}`)) }, 'Remove'))))))) : null,
  ];
}

async function adminEmail() {
  const { messages } = await get('/api/admin/outbox');
  return [
    h('p', { class: 'muted' }, 'There is no mail server yet, so emails are kept here (and in the outbox folder of the server\'s data) instead of being sent.'),
    messages.length ? messages.map((m) => h('div', { class: 'panel stack mail' },
      h('div', { class: 'row' }, h('strong', {}, m.subject), h('div', { class: 'grow' }), h('span', { class: 'muted small' }, `to ${m.to}`)),
      h('pre', { class: 'mail-text' }, m.text))) : h('div', { class: 'empty' }, icon('file'), h('h2', {}, 'No emails yet')),
  ];
}

async function adminAudit() {
  const { entries } = await get('/api/admin/audit');
  return [h('div', { class: 'table-wrap' }, h('table', { class: 'table' },
    h('thead', {}, h('tr', {}, ['When', 'Who', 'What', 'On', 'Details'].map((t) => h('th', {}, t)))),
    h('tbody', {}, entries.map((e) => h('tr', {},
      h('td', { title: dateTime(e.ts) }, timeAgo(e.ts)),
      h('td', {}, e.actor ? e.actor.name : 'system'),
      h('td', {}, e.action),
      h('td', {}, e.targetKind === 'document' ? h('a', { href: `/d/${e.targetId}` }, 'document') : `${e.targetKind} ${e.targetId}`),
      h('td', { class: 'muted small' }, Object.keys(e.detail).length ? JSON.stringify(e.detail) : ''))))))];
}

/**
 * One account, for admins: who they are, their groups, and their documents,
 * which an admin opens passively (not a view, no lock, nothing saved).
 */
export async function renderAdminUser(target, id) {
  if (!state.me.user.isAdmin) {
    clear(target, h('div', { class: 'empty' }, icon('lock'), h('h2', {}, 'Admins only')));
    return;
  }
  const { user: u, groups, counts } = await get(`/api/admin/users/${id}`);
  const me = state.me.user;
  const button = (label, onclick, cls = '') => h('button', { type: 'button', class: `btn small-btn ${cls}`.trim(), onclick }, label);
  const before = h('div', { class: 'stack' },
    h('div', {}, h('a', { class: 'link small', href: '/admin' }, '← Accounts')),
    h('div', { class: 'page-head' }, h('span', { class: 'avatar large' }, avatar(u)), h('div', { class: 'grow' },
      h('h1', {}, u.name),
      h('div', { class: 'muted small' }, [`@${u.handle}`, u.email ?? 'no email', STATUS_LABELS[u.status] ?? u.status, `${u.effectivePlan.plan} plan`,
        u.isAdmin ? 'admin' : null].filter(Boolean).join(' · '))),
    u.id !== me.id ? h('button', { type: 'button', class: 'btn', onclick: () => newMessageDialog(u.name).catch(showError) }, icon('chat'), 'Message') : null),
    h('div', { class: 'admin-user' },
      h('div', { class: 'panel stack' },
        h('h2', {}, 'Account'),
        h('dl', { class: 'props' },
          h('dt', {}, 'Joined'), h('dd', {}, dateTime(u.createdAt)),
          h('dt', {}, 'Last seen'), h('dd', {}, u.lastSeenAt ? timeAgo(u.lastSeenAt) : 'never'),
          h('dt', {}, 'Storage'), h('dd', {}, `${bytes(u.bytes)}${u.quotaMB !== null ? ` of ${u.quotaMB} MB` : ''}`),
          h('dt', {}, 'Documents'), h('dd', {}, `${u.documents} (${counts.trashed} in the trash, ${counts.public} not private, ${counts.moderated} moderated)`),
          h('dt', {}, 'Messages'), h('dd', {}, `${counts.messages} sent · ${counts.reportsAgainst} report${counts.reportsAgainst === 1 ? '' : 's'} about them`),
          h('dt', {}, 'About'), h('dd', {}, u.bio || h('span', { class: 'muted' }, 'nothing'))),
        u.requestMessage ? h('blockquote', { class: 'request' }, u.requestMessage) : null,
        h('div', { class: 'row wrap' }, accountButtons(u, me),
          u.hasAvatar ? button('Clear picture', change(() => patch(`/api/admin/users/${u.id}`, { clearAvatar: true }))) : null,
          u.bio ? button('Clear about', change(() => patch(`/api/admin/users/${u.id}`, { clearBio: true }))) : null)),
      h('div', { class: 'panel stack' },
        h('h2', {}, 'Groups'),
        groups.length ? groups.map((g) => h('div', { class: 'row' }, h('a', { class: 'grow', href: `/groups/${g.id}` }, g.name),
          h('span', { class: 'muted small' }, g.owner ? 'owner' : g.role)))
          : h('div', { class: 'muted small' }, 'In no groups.'))),
    h('div', { class: 'note' }, 'Opening their documents is not counted as a view, takes no edit lock, and saves nothing: they go on working undisturbed. Each look is in the activity log.'));
  await renderListing(target, { view: 'user', userId: u.id, title: 'Their documents', before });
}

export async function renderAdmin(target, tab) {
  if (!state.me.user.isAdmin) {
    clear(target, h('div', { class: 'empty' }, icon('lock'), h('h2', {}, 'Admins only')));
    return;
  }
  const build = { users: adminUsers, invites: adminInvites, settings: adminSettings, reports: () => adminReports(change), fonts: adminFonts, email: adminEmail, audit: adminAudit }[tab] ?? adminUsers;
  const [content, stats] = await Promise.all([build(), get('/api/admin/stats')]);
  if (target.isConnected) {
    clear(target, h('div', { class: 'page-head' }, h('h1', {}, 'Admin')), adminTabs(tab, stats.waiting), content);
  }
}
