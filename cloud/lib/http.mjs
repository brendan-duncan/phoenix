// HTTP plumbing with no framework: a path router, JSON and body helpers,
// cookies, and static files with their precompressed copies.

import { once } from 'node:events';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';

export class HttpError extends Error {
  /**
   * @param {number} status
   * @param {string} code short machine-readable reason
   * @param {string} [message] for people
   */
  constructor(status, code, message = code) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

/** The answer while an admin has locked the site (Admin > Settings). */
export function lockedError(settings) {
  return new HttpError(503, 'locked', settings.lockMessage || 'Phoenix Cloud is closed for maintenance for a while. Try again later.');
}

export const badRequest = (message) => new HttpError(400, 'bad_request', message);
export const notFound = (message = 'Not found.') => new HttpError(404, 'not_found', message);
export const forbidden = (message = 'You do not have access to that.') => new HttpError(403, 'forbidden', message);

/** Routes like '/api/documents/:id/file'; params come back by name. */
export class Router {
  constructor() {
    this.routes = [];
  }

  add(method, pattern, handler) {
    const names = [];
    const source = pattern.replace(/:(\w+)/g, (_, name) => {
      names.push(name);
      return '([^/]+)';
    });
    this.routes.push({ method, regex: new RegExp(`^${source}$`), names, handler });
  }

  get(pattern, handler) { this.add('GET', pattern, handler); }
  post(pattern, handler) { this.add('POST', pattern, handler); }
  put(pattern, handler) { this.add('PUT', pattern, handler); }
  patch(pattern, handler) { this.add('PATCH', pattern, handler); }
  delete(pattern, handler) { this.add('DELETE', pattern, handler); }

  /** The handler and params for a request, or null; 405 when only the method is wrong. */
  match(method, pathname) {
    let pathMatched = false;
    for (const route of this.routes) {
      const m = route.regex.exec(pathname);
      if (!m) {
        continue;
      }
      pathMatched = true;
      if (route.method === method || (method === 'HEAD' && route.method === 'GET')) {
        const params = {};
        route.names.forEach((name, i) => {
          params[name] = decodeURIComponent(m[i + 1]);
        });
        return { handler: route.handler, params };
      }
    }
    if (pathMatched) {
      throw new HttpError(405, 'method_not_allowed', 'That method is not allowed here.');
    }
    return null;
  }
}

/** The request body as a Buffer, refusing more than `limit` bytes. */
export function readBody(req, limit) {
  return new Promise((resolve, reject) => {
    const declared = Number(req.headers['content-length']);
    if (Number.isFinite(declared) && declared > limit) {
      reject(tooLarge(limit));
      req.resume();
      return;
    }
    const parts = [];
    let size = 0;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > limit) {
        reject(tooLarge(limit));
        req.destroy();
        return;
      }
      parts.push(chunk);
    });
    req.on('end', () => resolve(Buffer.concat(parts)));
    req.on('error', reject);
  });
}

function tooLarge(limit) {
  return new HttpError(413, 'too_large', `That is larger than the ${Math.round(limit / 1048576)} MB limit.`);
}

/**
 * Streams the request body into `file`, refusing more than `limit` bytes;
 * its size. Nothing of it is held in memory. On any failure the file is
 * gone.
 */
export async function saveBody(req, file, limit) {
  const declared = Number(req.headers['content-length']);
  if (Number.isFinite(declared) && declared > limit) {
    req.resume();
    throw tooLarge(limit);
  }
  const out = fs.createWriteStream(file);
  let size = 0;
  try {
    for await (const piece of req) {
      size += piece.length;
      if (size > limit) {
        throw tooLarge(limit);
      }
      if (!out.write(piece)) {
        await once(out, 'drain');
      }
    }
    await new Promise((resolve, reject) => out.end((err) => (err ? reject(err) : resolve())));
    return size;
  } catch (err) {
    out.destroy();
    // Whatever is still coming is read and dropped, so the answer can be sent.
    req.resume();
    await fsp.rm(file, { force: true });
    throw err;
  }
}

export async function readJson(req, limit) {
  const type = req.headers['content-type'] ?? '';
  if (!type.startsWith('application/json')) {
    throw new HttpError(415, 'unsupported_type', 'Send JSON.');
  }
  const body = await readBody(req, limit);
  if (body.length === 0) {
    return {};
  }
  try {
    const value = JSON.parse(body.toString('utf8'));
    if (value === null || typeof value !== 'object' || Array.isArray(value)) {
      throw new Error('not an object');
    }
    return value;
  } catch {
    throw badRequest('The body is not a JSON object.');
  }
}

export function sendJson(res, status, body, headers = {}) {
  const text = JSON.stringify(body);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Length': Buffer.byteLength(text),
    ...headers,
  });
  res.end(text);
}

export function redirect(res, location, status = 302) {
  res.writeHead(status, { Location: location, 'Cache-Control': 'no-store' });
  res.end();
}

export function parseCookies(header) {
  const out = {};
  for (const part of (header ?? '').split(';')) {
    const eq = part.indexOf('=');
    if (eq > 0) {
      const name = part.slice(0, eq).trim();
      if (!(name in out)) {
        out[name] = part.slice(eq + 1).trim();
      }
    }
  }
  return out;
}

export function serializeCookie(name, value, { maxAge, secure, httpOnly = true, sameSite = 'Lax', path: cookiePath = '/' } = {}) {
  let cookie = `${name}=${value}; Path=${cookiePath}; SameSite=${sameSite}`;
  if (maxAge !== undefined) {
    cookie += `; Max-Age=${Math.floor(maxAge)}`;
  }
  if (httpOnly) {
    cookie += '; HttpOnly';
  }
  if (secure) {
    cookie += '; Secure';
  }
  return cookie;
}

export const CONTENT_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.txt': 'text/plain; charset=utf-8',
  '.woff2': 'font/woff2',
};

/**
 * Serves a file below `root`, choosing a Brotli or gzip copy beside it
 * (`name.br`, `name.gz`) when the browser takes one. Returns false when there
 * is no such file.
 */
export async function serveStatic(req, res, root, relative, headers = {}) {
  const file = path.resolve(root, `.${path.posix.normalize(`/${relative}`)}`);
  if (file !== root && !file.startsWith(root + path.sep)) {
    return false;
  }
  let stat;
  try {
    stat = await fsp.stat(file);
  } catch {
    return false;
  }
  if (!stat.isFile()) {
    return false;
  }
  const type = CONTENT_TYPES[path.extname(file).toLowerCase()] ?? 'application/octet-stream';
  const accept = req.headers['accept-encoding'] ?? '';
  let chosen = file;
  let encoding = null;
  for (const [ext, name] of [['.br', 'br'], ['.gz', 'gzip']]) {
    if (accept.includes(name) && fs.existsSync(file + ext)) {
      chosen = file + ext;
      encoding = name;
      break;
    }
  }
  const size = encoding ? (await fsp.stat(chosen)).size : stat.size;
  // Revalidated by the original's size and time, so a new build is fetched
  // and an unchanged one costs a 304.
  const etag = `"${stat.size.toString(16)}-${Math.floor(stat.mtimeMs).toString(16)}${encoding ? `-${encoding}` : ''}"`;
  const out = { 'Content-Type': type, Vary: 'Accept-Encoding', ETag: etag, ...headers };
  if (req.headers['if-none-match'] === etag) {
    res.writeHead(304, out);
    res.end();
    return true;
  }
  out['Content-Length'] = size;
  if (encoding) {
    out['Content-Encoding'] = encoding;
  }
  res.writeHead(200, out);
  if (req.method === 'HEAD') {
    res.end();
    return true;
  }
  await new Promise((resolve, reject) => {
    fs.createReadStream(chosen).on('error', reject).on('end', resolve).pipe(res);
  });
  return true;
}
