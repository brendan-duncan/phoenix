// Phoenix opens files people upload (FLA, SWF, SVG, EPS, AI) with native
// code, so on Linux it runs in a bubblewrap sandbox: a fresh, empty view of
// the system with the system's programs and libraries, Phoenix and the font
// library read-only, the job's input files read-only, and the job's folder
// the only place it can write. It has no network and sees no other process.
// The service's data (its database, everyone's documents) and settings
// (/etc/phoenix-cloud.env) are not there to read, so a file crafted to take
// over Phoenix gets nothing.
//
// PHOENIX_CLOUD_SANDBOX: 'auto' (bubblewrap when it is installed, on Linux),
// 'bwrap' (always; an error when missing) or 'none'.
// PHOENIX_CLOUD_SANDBOX_BIND: more folders to show read-only, colon-separated
// (by default the folder above Phoenix's own, for its libraries).

import fs from 'node:fs';
import path from 'node:path';

const BWRAP = ['/usr/bin/bwrap', '/bin/bwrap'];

/** The bubblewrap program when there is one; null otherwise. */
export function findBwrap() {
  return process.platform === 'linux' ? BWRAP.find((p) => fs.existsSync(p)) ?? null : null;
}

/** Whether conversions run sandboxed: 'bwrap' or 'none'. */
export function sandboxMode(setting) {
  if (setting === 'none') {
    return 'none';
  }
  if (setting === 'bwrap') {
    if (!findBwrap()) {
      throw new Error('PHOENIX_CLOUD_SANDBOX=bwrap, but bubblewrap is not installed (apt install bubblewrap)');
    }
    return 'bwrap';
  }
  return findBwrap() ? 'bwrap' : 'none';
}

/** The folders Phoenix's program needs: by default, the one above its own (bin/ and lib/). */
export function phoenixFolders(exe, extra = '') {
  const given = String(extra || '').split(':').map((p) => p.trim()).filter(Boolean);
  if (given.length) {
    return given;
  }
  let real = exe;
  try {
    real = fs.realpathSync(exe);
  } catch {
    // As given.
  }
  return [path.dirname(path.dirname(real))];
}

/**
 * bubblewrap's arguments to run `exe` with `args` in `dir` (writable), seeing
 * `readOnly` folders and `inputs` (files, or for an XFL document its folder)
 * read-only, with only `env` for its environment.
 */
export function sandboxArgs({ exe, args, dir, readOnly = [], inputs = [], env = {}, exists = fs.existsSync, isLink = isSymlink }) {
  const out = ['--unshare-all', '--die-with-parent', '--new-session', '--clearenv'];
  // The system's programs and libraries. On merged-/usr systems /bin, /lib
  // and the rest are links into /usr, and stay links.
  out.push('--ro-bind', '/usr', '/usr');
  for (const top of ['/bin', '/sbin', '/lib', '/lib32', '/lib64', '/libx32']) {
    const link = isLink(top);
    if (link) {
      out.push('--symlink', link, top);
    } else if (exists(top)) {
      out.push('--ro-bind', top, top);
    }
  }
  // Just what programs read of /etc: fonts, the library cache, the time zone.
  for (const file of ['/etc/fonts', '/etc/ld.so.cache', '/etc/ld.so.conf', '/etc/ld.so.conf.d', '/etc/localtime', '/etc/alternatives']) {
    out.push('--ro-bind-try', file, file);
  }
  out.push('--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp');
  for (const folder of readOnly) {
    out.push('--ro-bind', folder, folder);
  }
  for (const input of inputs) {
    // An XFL document is its folder; anything else, the one file.
    const shown = path.basename(input) === 'DOMDocument.xml' ? path.dirname(input) : input;
    out.push('--ro-bind', shown, shown);
  }
  out.push('--bind', dir, dir, '--chdir', dir);
  for (const [name, value] of Object.entries(env)) {
    out.push('--setenv', name, String(value));
  }
  out.push('--', exe, ...args);
  return out;
}

function isSymlink(file) {
  try {
    return fs.lstatSync(file).isSymbolicLink() ? fs.readlinkSync(file) : null;
  } catch {
    return null;
  }
}

/** The environment Phoenix gets outside a sandbox: the service's own, less its settings (secrets among them). */
export function plainEnv(env = process.env) {
  return Object.fromEntries(Object.entries(env).filter(([name]) => !name.startsWith('PHOENIX_CLOUD_')));
}
