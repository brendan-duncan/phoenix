// The request pipeline for the cloud origin (dashboard, API, editor) and the
// separate play origin. Every response's security headers are set here, the
// same in dev and production; in production Apache only adds TLS and proxies.

import fsp from 'node:fs/promises';
import path from 'node:path';
import { ensureDefaultAdmin, Sessions } from './accounts.mjs';
import { registerAdmin } from './api/admin.mjs';
import { registerAuth } from './api/auth.mjs';
import { registerDocuments } from './api/documents.mjs';
import { registerGroups } from './api/groups.mjs';
import { CLOUD_DIR, editorAvailable } from './config.mjs';
import { Converter } from './convert.mjs';
import { HttpError, notFound, readBody, readJson, Router, saveBody, sendJson, serveStatic } from './http.mjs';
import { newId } from './ids.mjs';
import { Attempts, Budget } from './limits.mjs';
import { Mailer } from './mail.mjs';
import { openPass } from './renders.mjs';
import { Starters } from './starters.mjs';
import { Jwks } from './oauth.mjs';

const DASHBOARD_DIR = path.join(CLOUD_DIR, 'dashboard');
const SAFE_METHODS = new Set(['GET', 'HEAD']);

/** One request's view of the app: who is asking, and helpers for handlers. */
class Context {
  constructor(app, req, res, url, params, auth) {
    this.app = app;
    this.store = app.store;
    this.blobs = app.blobs;
    this.config = app.config;
    this.req = req;
    this.res = res;
    this.url = url;
    this.params = params;
    this.session = auth?.session ?? null;
    this.user = auth?.user ?? null;
    this.status = 200;
    this.settingsCache = null;
    this.temps = [];
  }

  get settings() {
    this.settingsCache ??= this.store.settings;
    return this.settingsCache;
  }

  /** A share link's token, from the page's address (passed on as a header or query). */
  get linkToken() {
    return this.req.headers['x-phoenix-link'] || this.url.searchParams.get('link') || null;
  }

  query(name) {
    return this.url.searchParams.get(name);
  }

  requireUser() {
    if (!this.user) {
      throw new HttpError(401, 'signed_out', 'Sign in first.');
    }
    return this.user;
  }

  requireAdmin() {
    const user = this.requireUser();
    if (!user.is_admin) {
      throw new HttpError(403, 'forbidden', 'Only admins can do that.');
    }
    return user;
  }

  json() {
    return readJson(this.req, this.config.maxJsonBytes);
  }

  body(limit = this.config.maxUploadBytes) {
    return readBody(this.req, limit);
  }

  /** A temporary file's path (with `ext`), deleted when the request ends. */
  temporary(ext = '') {
    const file = path.join(this.config.dataDir, 'uploads', `${newId()}${ext ? `.${ext}` : ''}`);
    this.temps.push(file);
    return file;
  }

  /** Saves the request body to a temporary file (named with `ext`): { file, size }. */
  async upload(ext = '', limit = this.config.maxUploadBytes) {
    await fsp.mkdir(path.join(this.config.dataDir, 'uploads'), { recursive: true });
    const file = this.temporary(ext);
    const size = await saveBody(this.req, file, limit);
    return { file, size };
  }

  /** Deletes the temporary files this request made. */
  async cleanup() {
    await Promise.all(this.temps.map((file) => fsp.rm(file, { force: true })));
  }
}

export function createApp({ config, store, blobs }) {
  const router = new Router();
  const app = {
    config,
    store,
    blobs,
    router,
    sessions: new Sessions(store, config),
    jwks: new Jwks(),
    converter: new Converter(config),
    starters: new Starters(config.resourcesDir),
    mailer: new Mailer(config),
    attempts: new Attempts(),
    // Documents made or converted by Phoenix, per person per hour.
    conversions: new Budget(config.conversionsPerHour, 60 * 60 * 1000),
  };
  // A server with no accounts gets the admin @phoenix (password "animate").
  app.ready = ensureDefaultAdmin(store).then((made) => {
    if (made) {
      console.log('[cloud] made the admin account @phoenix with the password "animate"; change it after signing in');
    }
  });
  registerAuth(router, app);
  registerDocuments(router, app);
  registerGroups(router, app);
  registerAdmin(router, app);
  app.handle = (req, res) => handleCloud(app, req, res);
  app.handlePlay = (req, res) => handlePlay(app, req, res);
  return app;
}

function commonHeaders(app, res) {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'same-origin');
  if (app.store.settings.beta) {
    res.setHeader('X-Robots-Tag', 'noindex, nofollow');
  }
}

function pageHeaders(app) {
  const csp = [
    "default-src 'self'",
    "script-src 'self'",
    "style-src 'self'",
    // Avatars come from the sign-in providers.
    "img-src 'self' data: https://lh3.googleusercontent.com https://avatars.githubusercontent.com",
    "connect-src 'self'",
    `frame-src ${app.config.playUrl}`,
    "frame-ancestors 'none'",
    "base-uri 'none'",
    "form-action 'self'",
    "object-src 'none'",
  ].join('; ');
  return { 'Content-Security-Policy': csp, 'Cache-Control': 'no-cache' };
}

function sendError(res, err) {
  if (res.headersSent) {
    res.destroy();
    return;
  }
  if (err instanceof HttpError) {
    sendJson(res, err.status, { error: err.code, message: err.message });
    return;
  }
  console.error('[cloud]', err);
  sendJson(res, 500, { error: 'internal', message: 'Something went wrong on the server.' });
}

async function handleCloud(app, req, res) {
  let url;
  try {
    url = new URL(req.url, app.config.publicUrl);
  } catch {
    sendJson(res, 400, { error: 'bad_request', message: 'Bad address.' });
    return;
  }
  try {
    commonHeaders(app, res);
    const p = url.pathname;
    if (p.startsWith('/api/')) {
      await handleApi(app, req, res, url);
      return;
    }
    if (!SAFE_METHODS.has(req.method)) {
      throw new HttpError(405, 'method_not_allowed', 'That method is not allowed here.');
    }
    if (p === '/editor') {
      res.writeHead(301, { Location: `/editor/${url.search}` });
      res.end();
      return;
    }
    if (p.startsWith('/editor/')) {
      await serveEditor(app, req, res, p.slice('/editor/'.length));
      return;
    }
    if (p === '/robots.txt') {
      const body = app.store.settings.beta ? 'User-agent: *\nDisallow: /\n' : 'User-agent: *\nDisallow: /api/\n';
      res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end(body);
      return;
    }
    if (p.startsWith('/ui/')) {
      if (!(await serveStatic(req, res, DASHBOARD_DIR, p.slice('/ui/'.length), pageHeaders(app)))) {
        throw notFound();
      }
      return;
    }
    // Every other address is a page of the dashboard, which routes itself.
    await serveStatic(req, res, DASHBOARD_DIR, 'index.html', pageHeaders(app));
  } catch (err) {
    sendError(res, err);
  }
}

async function handleApi(app, req, res, url) {
  const match = app.router.match(req.method, url.pathname);
  if (!match) {
    throw notFound('No such API.');
  }
  const auth = app.sessions.fromRequest(req);
  const ctx = new Context(app, req, res, url, match.params, auth);
  if (!SAFE_METHODS.has(req.method)) {
    // Changes come only from our own pages: the Origin must be ours, and a
    // signed-in request must carry its session's CSRF token.
    const origin = req.headers.origin;
    if (origin && origin !== new URL(app.config.publicUrl).origin) {
      throw new HttpError(403, 'origin', 'That request came from another site.');
    }
    if (auth) {
      app.sessions.checkCsrf(auth.session, req.headers['x-csrf-token']);
    } else if (!match.handler.anonymous) {
      throw new HttpError(401, 'signed_out', 'Sign in first.');
    }
  }
  try {
    const result = await match.handler(ctx);
    if (result !== undefined && !res.headersSent) {
      sendJson(res, ctx.status, result);
    }
  } finally {
    await ctx.cleanup();
  }
}

async function serveEditor(app, req, res, relative) {
  // Threads need cross-origin isolation; only the editor has it, as sign-in
  // pages break under it.
  const headers = {
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'Cross-Origin-Resource-Policy': 'same-origin',
    'Cache-Control': 'no-cache',
  };
  if (!relative || relative === 'index.html') {
    // The editor's page, with the script that opens and saves cloud
    // documents (it hands Phoenix its file as the page's host).
    let page;
    try {
      page = await fsp.readFile(path.join(app.config.editorDir, 'index.html'), 'utf8');
    } catch {
      page = null;
    }
    if (page !== null) {
      const body = page.replace('</head>', '<script src="/ui/editor-host.js"></script>\n</head>');
      res.writeHead(200, { ...headers, 'Content-Type': 'text/html; charset=utf-8', 'Content-Length': Buffer.byteLength(body) });
      res.end(req.method === 'HEAD' ? undefined : body);
      return;
    }
  }
  if (await serveStatic(req, res, app.config.editorDir, relative || 'index.html', headers)) {
    return;
  }
  if (!editorAvailable(app.config)) {
    res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end(`No web editor build at ${app.config.editorDir}.\nBuild it with: python tools/web/build_web.py\n`);
    return;
  }
  throw notFound();
}

/**
 * The play origin: HTML5 bundles of documents, for people who may play a
 * document but not open it. It has no cookies and no API; a bundle is
 * reached by a signed pass in its address (renders.mjs), so its scripts,
 * which come from the document, can reach nothing of the cloud's.
 */
async function handlePlay(app, req, res) {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('X-Robots-Tag', 'noindex, nofollow');
  if (!SAFE_METHODS.has(req.method)) {
    res.writeHead(405).end();
    return;
  }
  const match = /^\/b\/([A-Za-z0-9_-]+\.[A-Za-z0-9_-]+)\/(.+)$/.exec(new URL(req.url, app.config.playUrl).pathname);
  const dir = match ? openPass(app, match[1]) : null;
  if (dir && await serveStatic(req, res, dir, decodeURIComponent(match[2]), {
    'Cache-Control': 'private, max-age=3600',
    // Shown in the cloud's player page, from another origin.
    'Cross-Origin-Resource-Policy': 'cross-origin',
  })) {
    return;
  }
  res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end('Nothing here, or the link has run out. Open the document again in Phoenix Cloud.\n');
}
