# Phoenix Cloud

Accounts, documents, sharing and groups for Phoenix, served to the web
editor and a file-browser dashboard. A Node service with no dependencies
beyond Node itself (22.13 or newer, for `node:sqlite`): SQLite for records,
a folder of content-addressed files for document bytes.

It runs on this PC first. Nothing here needs a remote server yet.

## Running it locally

```sh
node cloud/server.mjs dev                  # http://localhost:8792
node cloud/server.mjs seed D:/fla/*.phx    # sample people, a group, documents
```

`dev` runs everything on loopback: the dashboard and API at
`http://localhost:8792`, the editor build at `/editor/` (from
`build-wasm32/webeditor`, built by `tools/web/build_web.py`), and the
separate play origin at `http://127.0.0.1:8793`. Data goes in
`cloud/dev-data/` (gitignored); `node cloud/server.mjs reset` deletes it.

Sign in with **dev login**, which takes any handle and creates the person
(the first is an admin). `seed` makes @alice (admin), @bob, @carol and the
group @studio, with documents shared between them. Use one browser profile
or private window per person to try sharing.

Real Google and GitHub sign-in work on localhost too, with OAuth clients
of their own for development. Put them in `cloud/dev.env` (gitignored):

```sh
PHOENIX_CLOUD_GOOGLE_ID=...
PHOENIX_CLOUD_GOOGLE_SECRET=...
PHOENIX_CLOUD_GITHUB_ID=...
PHOENIX_CLOUD_GITHUB_SECRET=...
```

with the redirect URIs `http://localhost:8792/api/auth/callback/google` and
`.../github`.

## Commands

| Command | Does |
|---|---|
| `dev` | Runs on this PC, with dev login. |
| `serve` | Runs in production; needs `PHOENIX_CLOUD_DATA` and `PHOENIX_CLOUD_URL`. |
| `make-admin --email E` | Prints an invite link that makes whoever claims it (signed in as E) an admin. |
| `make-admin --handle H` | Makes an existing person an admin. |
| `invite [--email E] [--note N] [--days D]` | Prints a beta invite link. |
| `seed [file.phx ...]` | Dev sample data. |
| `gc` | Deletes stored chunks that no revision uses. |
| `status` | Totals. |
| `reset` | Deletes the dev data. |

Commands other than `serve` work on the dev data unless
`PHOENIX_CLOUD_MODE=production` is set.

## Settings

From the environment (or `cloud/dev.env`):

| Variable | Default |
|---|---|
| `PHOENIX_CLOUD_MODE` | `production` for `serve`, `dev` otherwise |
| `PHOENIX_CLOUD_URL` | `http://localhost:8792`; the address people use |
| `PHOENIX_CLOUD_PLAY_URL` | `http://127.0.0.1:8793`; the play origin |
| `PHOENIX_CLOUD_PORT`, `_PLAY_PORT`, `_BIND` | `8792`, port + 1, `127.0.0.1` |
| `PHOENIX_CLOUD_DATA` | `cloud/dev-data` (required in production) |
| `PHOENIX_CLOUD_EDITOR` | `build-wasm32/webeditor` |
| `PHOENIX_CLOUD_MAX_UPLOAD_MB` | `1024` |
| `PHOENIX_CLOUD_<PROVIDER>_ID`, `_SECRET` | Google and GitHub sign-in |

Admins change the rest while it runs (Admin page or `PATCH
/api/admin/settings`): the sign-up policy (`invite_only`, the closed beta;
`open`; `closed`), the beta switch, the default storage quota and a banner.

## How it works

| File | Role |
|---|---|
| `server.mjs` | Commands; starts the cloud and play listeners. |
| `lib/app.mjs` | Request pipeline: security headers, CSRF and origin checks, the editor and dashboard files. |
| `lib/http.mjs` | Router, bodies, JSON, cookies, static files with Brotli/gzip copies. |
| `lib/store.mjs` | SQLite schema (migrations by `user_version`) and runtime settings. |
| `lib/accounts.mjs` | Users, sessions, invites, and deciding who a sign-in belongs to. |
| `lib/oauth.mjs` | Authorization code flow with PKCE; ID token checks against the provider's keys. |
| `lib/access.mjs` | Every access decision: roles on documents and spaces. |
| `lib/phx.mjs` | Reads, checks, splits and rebuilds `.phx` containers. |
| `lib/storage.mjs` | Revisions, quotas, rebuilding files, garbage collection. |
| `lib/blobs.mjs` | Content-addressed files on disk. |
| `lib/api/*.mjs` | The API: auth, documents (with folders, shares, links, favorites), groups, admin. |
| `dashboard/` | The file browser (plain JavaScript). |
| `tests/` | `node --test` suites; each runs a server in-process on a temp folder. |

**Documents are stored by chunk.** A `.phx` is a container of chunks
(docs/phx-format.md). On upload the server checks the container is whole,
stores each chunk's stored bytes once under its SHA-256, and records the
revision as a manifest of those hashes. A download rebuilds the file from
the manifest exactly as Phoenix wrote it. Revisions and copies share every
chunk they have in common, so keeping every revision is cheap and a
duplicate costs nothing until it changes. The server reads only META (the
application that wrote it) and THMB (the thumbnail); it needs none of
Phoenix's code.

**Access.** Documents and folders belong to a person or a group. A
person's role on a document (viewer, commenter, editor, owner) is the
highest of: owning it or being an admin of the owning group; the group's
default role for its members; shares to them or their groups; a share
link; and viewer for public, showcase and unlisted documents. While the
beta switch is on, links and visibility work only for people who are
signed in. `lib/access.mjs` is the only place this is decided.

**Groups.** The creator is the first admin; a group can have many admins,
and its last admin can neither leave nor be demoted. Members get the
group's default role on its files. Deleting a group gives its files to the
admin who deletes it, in a folder named after the group.

**Sign-in.** Full-page redirects only (no pop-ups, no provider scripts),
since the editor's cross-origin isolation breaks them; only `/editor/`
sends the isolation headers. A new sign-in whose email matches an existing
person is not linked silently: they sign in the old way and link it from
their account page. During the beta a new person needs an invite, which
can be held to their email.

**Sessions** are random tokens in an HttpOnly cookie, stored hashed.
Every change must come from our origin and carry the session's CSRF token.

## Tests

```sh
cd cloud && node --test "tests/*.test.mjs"
```

`phx.test.mjs` also round-trips `D:/fla/tutorial1.phx` (or
`PHOENIX_CLOUD_TEST_PHX`) when it is there. `oauth.test.mjs` runs a mock
OpenID provider with its own signing key.
