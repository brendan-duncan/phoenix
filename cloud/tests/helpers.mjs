// A cloud server in this process on a private temp folder, and a client that
// keeps one person's cookie and CSRF token.

import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { createApp } from '../lib/app.mjs';
import { BlobStore } from '../lib/blobs.mjs';
import { loadConfig } from '../lib/config.mjs';
import { buildContainer } from '../lib/phx.mjs';
import { sha256 } from '../lib/ids.mjs';
import { Store } from '../lib/store.mjs';

export async function startServer({ env = {} } = {}) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'phoenix-cloud-'));
  const config = loadConfig({ PHOENIX_CLOUD_DATA: dir, PHOENIX_CLOUD_EDITOR: path.join(dir, 'no-editor'), ...env }, { mode: 'dev' });
  const store = new Store(path.join(dir, 'cloud.db'));
  const blobs = new BlobStore(path.join(dir, 'blobs'));
  const app = createApp({ config, store, blobs });
  const server = http.createServer(app.handle);
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  config.publicUrl = base;
  return {
    base,
    config,
    store,
    blobs,
    app,
    dir,
    client: () => new Client(base),
    async close() {
      await new Promise((resolve) => server.close(resolve));
      store.close();
      fs.rmSync(dir, { recursive: true, force: true });
    },
  };
}

export class Client {
  constructor(base) {
    this.base = base;
    this.cookie = '';
    this.csrf = null;
    this.link = null;
  }

  async request(method, route, { json, body, headers = {}, redirect = 'manual' } = {}) {
    const h = { ...headers };
    if (this.cookie) {
      h.Cookie = this.cookie;
    }
    if (this.csrf && method !== 'GET') {
      h['X-CSRF-Token'] = this.csrf;
    }
    if (this.link) {
      h['X-Phoenix-Link'] = this.link;
    }
    let payload = body;
    if (json !== undefined) {
      h['Content-Type'] = 'application/json';
      payload = JSON.stringify(json);
    }
    const res = await fetch(this.base + route, { method, headers: h, body: payload, redirect });
    const set = res.headers.get('set-cookie');
    if (set) {
      const [pair] = set.split(';');
      this.cookie = pair.endsWith('=') ? '' : pair;
    }
    return res;
  }

  /** JSON from a request that should succeed with `status`. */
  async ok(method, route, options = {}, status = [200, 201]) {
    const res = await this.request(method, route, options);
    const text = await res.text();
    const wanted = Array.isArray(status) ? status : [status];
    if (!wanted.includes(res.status)) {
      throw new Error(`${method} ${route} → ${res.status}: ${text}`);
    }
    return text ? JSON.parse(text) : null;
  }

  /** The error code of a request that should fail with `status`. */
  async fails(method, route, status, options = {}) {
    const res = await this.request(method, route, options);
    const text = await res.text();
    if (res.status !== status) {
      throw new Error(`${method} ${route} → ${res.status}, wanted ${status}: ${text}`);
    }
    return text ? JSON.parse(text).error : null;
  }

  async signIn(handle) {
    await this.ok('POST', '/api/auth/dev', { json: { handle } });
    const me = await this.ok('GET', '/api/me');
    this.csrf = me.csrf;
    this.user = me.user;
    return me.user;
  }

  get(route, options) { return this.ok('GET', route, options); }
  post(route, json = {}) { return this.ok('POST', route, { json }); }
  patch(route, json = {}) { return this.ok('PATCH', route, { json }); }
  del(route) { return this.ok('DELETE', route); }

  upload(name, bytes, query = '') {
    return this.ok('POST', `/api/documents/upload?name=${encodeURIComponent(name)}${query}`, { body: bytes, headers: { 'Content-Type': 'application/octet-stream' } });
  }
}

function protoString(field, text) {
  const value = Buffer.from(text, 'utf8');
  return Buffer.concat([Buffer.from([(field << 3) | 2, value.length]), value]);
}

/**
 * A small but real .phx container: a deflated DOCU, a SYMB per symbol (so
 * documents can share chunks), a THMB PNG and a META naming the application.
 */
export async function makePhx({ symbols = ['Symbol 1'], document = 'document', thumbnail = true } = {}) {
  const chunks = [];
  const bodies = new Map();
  const add = (t, content, { deflate = false, critical = true, symbolName } = {}) => {
    const stored = deflate ? zlib.deflateSync(content, { level: 3 }) : content;
    const h = sha256(stored);
    bodies.set(h, stored);
    const entry = { t, f: (critical ? 0x10 : 0) | (deflate ? 1 : 0), n: content.length, s: stored.length, c: zlib.crc32(stored) >>> 0, h };
    if (symbolName) {
      entry.y = symbolName;
    }
    chunks.push(entry);
  };
  add('DOCU', Buffer.from(`${document} `.repeat(40)), { deflate: true });
  for (const name of symbols) {
    add('SYMB', Buffer.from(`symbol ${name} `.repeat(30)), { deflate: true, symbolName: name });
  }
  if (thumbnail) {
    add('THMB', Buffer.from('89504e470d0a1a0a0000000d49484452', 'hex'), { critical: false });
  }
  add('META', Buffer.concat([protoString(1, 'Phoenix 0.22.0'), protoString(4, 'Tester')]), { critical: false });
  const pieces = await buildContainer({ v: 1, major: 1, minor: 0, chunks }, (h) => bodies.get(h));
  return Buffer.concat(pieces);
}
