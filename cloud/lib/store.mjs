// SQLite storage (node:sqlite, no dependencies): one file, WAL mode. The
// schema is applied as numbered migrations tracked in PRAGMA user_version, so
// a database from an older build is brought up to date on open.
//
// Owners: documents and folders belong to a user or a group, written as
// owner_kind ('user' | 'group') + owner_id. Shares name a principal the same
// way. Times are milliseconds since the epoch.

import fs from 'node:fs';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';

const MIGRATIONS = [
  `
CREATE TABLE users (
  id TEXT PRIMARY KEY,
  handle TEXT NOT NULL UNIQUE COLLATE NOCASE,
  name TEXT NOT NULL,
  email TEXT COLLATE NOCASE,
  avatar_url TEXT NOT NULL DEFAULT '',
  bio TEXT NOT NULL DEFAULT '',
  is_admin INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'active',
  quota_mb INTEGER,
  created_at INTEGER NOT NULL,
  last_seen_at INTEGER
);
CREATE INDEX users_email ON users (email);

CREATE TABLE identities (
  provider TEXT NOT NULL,
  subject TEXT NOT NULL,
  user_id TEXT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
  email TEXT,
  created_at INTEGER NOT NULL,
  last_used_at INTEGER,
  PRIMARY KEY (provider, subject)
);
CREATE INDEX identities_user ON identities (user_id);

CREATE TABLE sessions (
  id_hash TEXT PRIMARY KEY,
  user_id TEXT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
  csrf TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  last_seen_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  user_agent TEXT NOT NULL DEFAULT ''
);
CREATE INDEX sessions_user ON sessions (user_id);

CREATE TABLE login_states (
  state TEXT PRIMARY KEY,
  provider TEXT NOT NULL,
  verifier TEXT NOT NULL,
  nonce TEXT NOT NULL,
  invite TEXT,
  link_user_id TEXT,
  return_to TEXT NOT NULL DEFAULT '/',
  created_at INTEGER NOT NULL
);

CREATE TABLE invites (
  id TEXT PRIMARY KEY,
  token_hash TEXT NOT NULL UNIQUE,
  email TEXT COLLATE NOCASE,
  note TEXT NOT NULL DEFAULT '',
  make_admin INTEGER NOT NULL DEFAULT 0,
  created_by TEXT,
  created_at INTEGER NOT NULL,
  expires_at INTEGER,
  claimed_by TEXT,
  claimed_at INTEGER,
  revoked_at INTEGER
);

CREATE TABLE groups (
  id TEXT PRIMARY KEY,
  handle TEXT NOT NULL UNIQUE COLLATE NOCASE,
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  default_role TEXT NOT NULL DEFAULT 'editor',
  join_policy TEXT NOT NULL DEFAULT 'invite',
  created_by TEXT,
  created_at INTEGER NOT NULL
);

CREATE TABLE group_members (
  group_id TEXT NOT NULL REFERENCES groups (id) ON DELETE CASCADE,
  user_id TEXT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
  role TEXT NOT NULL,
  added_by TEXT,
  added_at INTEGER NOT NULL,
  PRIMARY KEY (group_id, user_id)
) WITHOUT ROWID;
CREATE INDEX group_members_user ON group_members (user_id);

CREATE TABLE folders (
  id TEXT PRIMARY KEY,
  owner_kind TEXT NOT NULL,
  owner_id TEXT NOT NULL,
  parent_id TEXT REFERENCES folders (id) ON DELETE CASCADE,
  name TEXT NOT NULL,
  created_by TEXT,
  created_at INTEGER NOT NULL
);
CREATE INDEX folders_owner ON folders (owner_kind, owner_id, parent_id);

CREATE TABLE documents (
  id TEXT PRIMARY KEY,
  owner_kind TEXT NOT NULL,
  owner_id TEXT NOT NULL,
  folder_id TEXT REFERENCES folders (id) ON DELETE SET NULL,
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  visibility TEXT NOT NULL DEFAULT 'private',
  allow_download INTEGER NOT NULL DEFAULT 1,
  allow_duplicate INTEGER NOT NULL DEFAULT 1,
  revision INTEGER NOT NULL DEFAULT 0,
  size INTEGER NOT NULL DEFAULT 0,
  thumbnail TEXT,
  info TEXT NOT NULL DEFAULT '{}',
  created_by TEXT,
  created_at INTEGER NOT NULL,
  modified_at INTEGER NOT NULL,
  modified_by TEXT,
  trashed_at INTEGER,
  forked_from TEXT
);
CREATE INDEX documents_owner ON documents (owner_kind, owner_id, folder_id);

CREATE TABLE revisions (
  id INTEGER PRIMARY KEY,
  document_id TEXT NOT NULL REFERENCES documents (id) ON DELETE CASCADE,
  number INTEGER NOT NULL,
  created_by TEXT,
  created_at INTEGER NOT NULL,
  label TEXT NOT NULL DEFAULT '',
  manifest TEXT NOT NULL,
  size INTEGER NOT NULL,
  application TEXT NOT NULL DEFAULT '',
  UNIQUE (document_id, number)
);

CREATE TABLE revision_chunks (
  revision_id INTEGER NOT NULL REFERENCES revisions (id) ON DELETE CASCADE,
  hash TEXT NOT NULL,
  PRIMARY KEY (revision_id, hash)
) WITHOUT ROWID;
CREATE INDEX revision_chunks_hash ON revision_chunks (hash);

CREATE TABLE blobs (
  hash TEXT PRIMARY KEY,
  size INTEGER NOT NULL,
  created_at INTEGER NOT NULL
) WITHOUT ROWID;

CREATE TABLE shares (
  document_id TEXT NOT NULL REFERENCES documents (id) ON DELETE CASCADE,
  principal_kind TEXT NOT NULL,
  principal_id TEXT NOT NULL,
  role TEXT NOT NULL,
  granted_by TEXT,
  granted_at INTEGER NOT NULL,
  PRIMARY KEY (document_id, principal_kind, principal_id)
) WITHOUT ROWID;
CREATE INDEX shares_principal ON shares (principal_kind, principal_id);

CREATE TABLE share_links (
  id TEXT PRIMARY KEY,
  document_id TEXT NOT NULL REFERENCES documents (id) ON DELETE CASCADE,
  token_hash TEXT NOT NULL UNIQUE,
  role TEXT NOT NULL,
  created_by TEXT,
  created_at INTEGER NOT NULL,
  expires_at INTEGER,
  revoked_at INTEGER
);
CREATE INDEX share_links_document ON share_links (document_id);

CREATE TABLE favorites (
  user_id TEXT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
  kind TEXT NOT NULL,
  target_id TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (user_id, kind, target_id)
) WITHOUT ROWID;

CREATE TABLE recents (
  user_id TEXT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
  document_id TEXT NOT NULL REFERENCES documents (id) ON DELETE CASCADE,
  opened_at INTEGER NOT NULL,
  PRIMARY KEY (user_id, document_id)
) WITHOUT ROWID;
CREATE INDEX recents_user ON recents (user_id, opened_at);

CREATE TABLE audit_log (
  id INTEGER PRIMARY KEY,
  ts INTEGER NOT NULL,
  actor_id TEXT,
  action TEXT NOT NULL,
  target_kind TEXT NOT NULL DEFAULT '',
  target_id TEXT NOT NULL DEFAULT '',
  detail TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX audit_log_ts ON audit_log (ts);

CREATE TABLE settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
`,
  // Passwords and registration. A user's status is 'active', 'pending_email'
  // (waiting to confirm their address), 'pending_admin' (waiting for an
  // admin to unlock the account) or 'suspended'.
  `
ALTER TABLE users ADD COLUMN password_hash TEXT;
ALTER TABLE users ADD COLUMN password_is_default INTEGER NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN email_verified INTEGER NOT NULL DEFAULT 0;

CREATE TABLE email_tokens (
  token_hash TEXT PRIMARY KEY,
  user_id TEXT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
  purpose TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  used_at INTEGER
);
CREATE INDEX email_tokens_user ON email_tokens (user_id);
DELETE FROM settings WHERE key = 'signupPolicy';
`,
];

/** Settings an admin changes at runtime; stored as JSON in `settings`. */
export const SETTING_DEFAULTS = {
  // What a new account is when someone registers: 'open' (ready to use),
  // 'email' (locked until they confirm their email address) or 'admin'
  // (locked until an admin unlocks it). Someone with an invite is let in
  // straight away.
  newAccounts: 'admin',
  // While on, public, showcase and unlisted documents are visible to signed-in
  // members only, and every page asks search engines not to index it.
  beta: true,
  defaultQuotaMB: 2048,
  banner: '',
};

export class Store {
  /** @param {string} file the database path, or ':memory:' */
  constructor(file) {
    if (file !== ':memory:') {
      fs.mkdirSync(path.dirname(file), { recursive: true });
    }
    this.db = new DatabaseSync(file);
    this.db.exec('PRAGMA journal_mode = WAL');
    this.db.exec('PRAGMA synchronous = NORMAL');
    this.db.exec('PRAGMA foreign_keys = ON');
    this.db.exec('PRAGMA busy_timeout = 5000');
    this.statements = new Map();
    this.migrate();
  }

  migrate() {
    const version = this.db.prepare('PRAGMA user_version').get().user_version;
    for (let v = version; v < MIGRATIONS.length; v++) {
      this.transaction(() => {
        this.db.exec(MIGRATIONS[v]);
        this.db.exec(`PRAGMA user_version = ${v + 1}`);
      });
    }
  }

  prepare(sql) {
    let statement = this.statements.get(sql);
    if (!statement) {
      statement = this.db.prepare(sql);
      this.statements.set(sql, statement);
    }
    return statement;
  }

  get(sql, ...params) {
    return this.prepare(sql).get(...params.map(bindable));
  }

  all(sql, ...params) {
    return this.prepare(sql).all(...params.map(bindable));
  }

  run(sql, ...params) {
    return this.prepare(sql).run(...params.map(bindable));
  }

  /** Runs `fn` in one transaction; nested calls join the outer one. */
  transaction(fn) {
    if (this.inTransaction) {
      return fn();
    }
    this.db.exec('BEGIN IMMEDIATE');
    this.inTransaction = true;
    try {
      const result = fn();
      this.db.exec('COMMIT');
      return result;
    } catch (err) {
      this.db.exec('ROLLBACK');
      throw err;
    } finally {
      this.inTransaction = false;
    }
  }

  get settings() {
    const out = { ...SETTING_DEFAULTS };
    for (const row of this.all('SELECT key, value FROM settings')) {
      if (row.key in SETTING_DEFAULTS) {
        out[row.key] = JSON.parse(row.value);
      }
    }
    return out;
  }

  saveSettings(values) {
    this.transaction(() => {
      for (const [key, value] of Object.entries(values)) {
        this.run('INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT (key) DO UPDATE SET value = excluded.value', key, JSON.stringify(value));
      }
    });
  }

  audit(actorId, action, targetKind = '', targetId = '', detail = {}) {
    this.run('INSERT INTO audit_log (ts, actor_id, action, target_kind, target_id, detail) VALUES (?, ?, ?, ?, ?, ?)', Date.now(), actorId, action, targetKind, targetId, JSON.stringify(detail));
  }

  close() {
    this.db.close();
  }
}

/** node:sqlite takes no booleans or undefined. */
function bindable(value) {
  if (value === undefined) {
    return null;
  }
  if (typeof value === 'boolean') {
    return value ? 1 : 0;
  }
  return value;
}
