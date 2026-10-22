// Settings fixed at startup, read from the environment. server.mjs loads an
// env file first (cloud/dev.env in dev, gitignored), so secrets such as OAuth
// client secrets never live in the repository. Settings an admin can change
// while the service runs live in the database instead (store.mjs).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const CLOUD_DIR = path.resolve(HERE, '..');
export const REPO_DIR = path.resolve(CLOUD_DIR, '..');

function envInt(env, name, fallback) {
  const n = Number.parseInt(env[name] ?? '', 10);
  return Number.isFinite(n) ? n : fallback;
}

/** OAuth providers known by name; each is enabled when its client id is set. */
const PROVIDERS = {
  google: {
    name: 'Google',
    kind: 'oidc',
    authorizeUrl: 'https://accounts.google.com/o/oauth2/v2/auth',
    tokenUrl: 'https://oauth2.googleapis.com/token',
    jwksUrl: 'https://www.googleapis.com/oauth2/v3/certs',
    issuers: ['https://accounts.google.com', 'accounts.google.com'],
    scope: 'openid email profile',
  },
  github: {
    name: 'GitHub',
    kind: 'github',
    authorizeUrl: 'https://github.com/login/oauth/authorize',
    tokenUrl: 'https://github.com/login/oauth/access_token',
    userUrl: 'https://api.github.com/user',
    emailsUrl: 'https://api.github.com/user/emails',
    scope: 'read:user user:email',
  },
};

/**
 * The service's configuration from `env`.
 * @param {Record<string, string|undefined>} env
 * @param {{ mode?: string }} [overrides]
 */
export function loadConfig(env = process.env, overrides = {}) {
  const mode = overrides.mode ?? env.PHOENIX_CLOUD_MODE ?? 'production';
  if (mode !== 'dev' && mode !== 'production') {
    throw new Error(`PHOENIX_CLOUD_MODE must be dev or production, not '${mode}'`);
  }
  const port = envInt(env, 'PHOENIX_CLOUD_PORT', 8792);
  const playPort = envInt(env, 'PHOENIX_CLOUD_PLAY_PORT', port + 1);
  const publicUrl = (env.PHOENIX_CLOUD_URL ?? `http://localhost:${port}`).replace(/\/+$/, '');
  const playUrl = (env.PHOENIX_CLOUD_PLAY_URL ?? `http://127.0.0.1:${playPort}`).replace(/\/+$/, '');
  if (mode === 'production' && !env.PHOENIX_CLOUD_DATA) {
    throw new Error('PHOENIX_CLOUD_DATA must name the data folder in production');
  }
  const providers = {};
  for (const [id, provider] of Object.entries(PROVIDERS)) {
    const key = id.toUpperCase();
    const clientId = env[`PHOENIX_CLOUD_${key}_ID`];
    const clientSecret = env[`PHOENIX_CLOUD_${key}_SECRET`];
    if (clientId && clientSecret) {
      providers[id] = { ...provider, id, clientId, clientSecret };
    }
  }
  return {
    mode,
    bind: env.PHOENIX_CLOUD_BIND ?? '127.0.0.1',
    port,
    playPort,
    publicUrl,
    playUrl,
    // Session cookies are Secure (and __Host-) only when served over https,
    // so the dev server works on plain http://localhost.
    secureCookies: publicUrl.startsWith('https:'),
    dataDir: path.resolve(env.PHOENIX_CLOUD_DATA ?? path.join(CLOUD_DIR, 'dev-data')),
    editorDir: path.resolve(env.PHOENIX_CLOUD_EDITOR ?? path.join(REPO_DIR, 'build-wasm32', 'webeditor')),
    // Behind Apache in production, the client address is the last
    // X-Forwarded-For entry; in dev nothing forwards.
    trustProxy: env.PHOENIX_CLOUD_TRUST_PROXY ? env.PHOENIX_CLOUD_TRUST_PROXY === '1' : mode === 'production',
    providers,
    // Phoenix itself, run with no window to make new documents and to
    // convert uploads (FLA, SWF, SVG, EPS, AI) into .phx with a thumbnail.
    phoenix: findPhoenix(env.PHOENIX_CLOUD_PHOENIX),
    maxUploadBytes: envInt(env, 'PHOENIX_CLOUD_MAX_UPLOAD_MB', 1024) * 1024 * 1024,
    maxJsonBytes: 256 * 1024,
    sessionDays: 30,
    loginStateMinutes: 10,
  };
}

/** PHOENIX_CLOUD_PHOENIX, or a build in this repository; null when there is none. */
function findPhoenix(given) {
  if (given) {
    return fs.existsSync(given) ? path.resolve(given) : null;
  }
  const name = process.platform === 'win32' ? 'Phoenix.exe' : 'Phoenix';
  for (const config of ['Release', 'RelWithDebInfo', '.']) {
    const candidate = path.join(REPO_DIR, 'build', config, name);
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return null;
}

/** Whether a web editor build is there to serve. */
export function editorAvailable(config) {
  return fs.existsSync(path.join(config.editorDir, 'index.html'));
}
