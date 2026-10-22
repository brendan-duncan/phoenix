# Phoenix Cloud

Accounts, documents, sharing and groups for Phoenix, with a file-browser
dashboard and the web editor opening and saving cloud documents. A Node
service with no dependencies beyond Node itself (22.13 or newer, for
`node:sqlite`): SQLite for records, a folder of content-addressed files for
document bytes, and Phoenix itself, run with no window, to make new
documents and convert uploads.

It runs on this PC first. Nothing here needs a remote server yet.

## Running it locally

```sh
python tools/web/build_web.py                # the web editor, once (for Edit and Play)
node cloud/server.mjs dev                    # http://localhost:8792
```

Sign in as **phoenix** with the password **animate**: a server with no
accounts makes this admin account, and the dashboard asks you to choose
your own password until you do. Anyone can register an account; what it is
then (ready, waiting for its email address to be confirmed, or waiting for
an admin to unlock it, the default) is an admin setting.

`node cloud/server.mjs import D:/fla/*.fla` adds files to @phoenix's My
Files (or `--to HANDLE`'s), converting them as uploads are.

`dev` runs everything on loopback: the dashboard and API at
`http://localhost:8792`, the editor build at `/editor/` (from
`build-wasm32/webeditor`), and the separate play origin at
`http://127.0.0.1:8793`. Data goes in `cloud/dev-data/` (gitignored);
`node cloud/server.mjs reset` deletes it.

New documents and uploads other than `.phx` (FLA, SWF, SVG, EPS, AI) need
Phoenix: the server uses `build/Release/Phoenix.exe` (or RelWithDebInfo)
from this repository, or `PHOENIX_CLOUD_PHOENIX`. Without one, only `.phx`
files can be uploaded.

Google and GitHub sign-in work on localhost too, with OAuth clients of
their own for development, in `cloud/dev.env` (gitignored):

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
| `dev` | Runs on this PC. |
| `serve` | Runs in production; needs `PHOENIX_CLOUD_DATA` and `PHOENIX_CLOUD_URL`. |
| `import [--to NAME] FILE ...` | Adds files to an account (default @phoenix), converted by Phoenix. |
| `set-password --handle NAME --password P` | Sets an account's password (for when no admin can sign in). |
| `make-admin --handle NAME` | Makes an account an admin (and unlocks it). |
| `invite [--email E] [--note N] [--days D] [--admin]` | Prints an invite link: whoever registers from it is let in at once. |
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
| `PHOENIX_CLOUD_PHOENIX` | `build/Release/Phoenix.exe` in this repository |
| `PHOENIX_CLOUD_MAX_UPLOAD_MB` | `1024` |
| `PHOENIX_CLOUD_<PROVIDER>_ID`, `_SECRET` | Google and GitHub sign-in |

Admins change the rest while it runs (Admin > Settings): what a new account
is (`admin`: locked until an admin unlocks it; `email`: locked until its
address is confirmed; `open`), the beta switch, the default storage quota
and a banner.

## Accounts

- A person has one name: what others see and what they sign in with (or
  their email). Names are unique whatever the capitals: 2 to 30 letters or
  digits, with spaces, `.`, `-`, `_` or `'` between them, never `@`.
  `GET /api/auth/name?name=` says whether one is free as it is typed.
- Passwords are hashed with scrypt.
  Failed sign-ins are counted per name and per address, and slowed down
  after ten in fifteen minutes.
- Registering asks for a name, email and password (8 characters or more,
  typed twice); the button is on only when all of them are right. While an
  admin unlocks new accounts, the page says it is a closed beta and asks for
  a request message (who they are, to show they are a real person), which
  Admin > Accounts shows beside Unlock. An invite link (Admin > Invites)
  lets its holder in at once, with no message.
- Email: there is no mail server yet. Confirmation emails are written to
  the data folder's `outbox/`, listed under Admin > Email, and printed by
  `dev`.
- Admin > Accounts unlocks waiting accounts, makes accounts (ready at
  once), sets passwords, suspends, makes admins, sets storage quotas and
  deletes accounts with what they own. An admin cannot suspend, demote or
  delete themself; an account that is the last admin of a group with other
  members can't be deleted until the group has another admin.

## Editing and playing

`/editor/?doc=ID` is the web editor on a cloud document, `&play=1` to play
it. The server serves the editor's page with `dashboard/editor-host.js`
added, which fetches the document, puts it in Phoenix's file system and
starts Phoenix on it (with `--play` to play); each Save in Phoenix comes
back to it (docs/web-editor.md, "A page that keeps documents") and is
uploaded as a new version. A save on top of someone else's newer one, or by
someone who can only view, offers to save a copy instead. A bar above
Phoenix shows the document and whether its last save reached the cloud.

## New documents and conversion

`lib/convert.mjs` runs Phoenix with no window, one job at a time, in a
folder of its own with settings of its own (the person's own Phoenix
settings are never touched), with a time limit:

- a new document: a JSFL script for `Phoenix run-script` makes it at the
  size, frame rate and background asked for;
- an FLA or SWF upload: the script opens it and saves it as `.phx`;
- an SVG, EPS or AI upload: the script imports it into a new document, the
  stage sized to an SVG's own page or fitted round the artwork;
- then `Phoenix render` draws the first frame 256 pixels wide, and the
  server puts it in the file as its thumbnail (THMB), as Phoenix's own save
  does. A `.phx` uploaded without a thumbnail gets one the same way.

Phoenix reads uploads that people send, so in production it should run as
a user of its own with no access to anything else.

## How it works

| File | Role |
|---|---|
| `server.mjs` | Commands; starts the cloud and play listeners. |
| `lib/app.mjs` | Request pipeline: security headers, CSRF and origin checks, the editor and dashboard files. |
| `lib/http.mjs` | Router, bodies, JSON, cookies, static files with Brotli/gzip copies. |
| `lib/store.mjs` | SQLite schema (migrations by `user_version`) and runtime settings. |
| `lib/accounts.mjs` | Users, sessions, registration, email tokens, invites, provider sign-ins. |
| `lib/passwords.mjs` | Password hashes. |
| `lib/limits.mjs` | Counting failed attempts. |
| `lib/mail.mjs` | The email outbox. |
| `lib/oauth.mjs` | Authorization code flow with PKCE; ID token checks against the provider's keys. |
| `lib/access.mjs` | Every access decision: roles on documents and spaces. |
| `lib/phx.mjs` | Reads, checks, splits and rebuilds `.phx` containers; adds thumbnails. |
| `lib/convert.mjs` | Phoenix with no window: new documents, conversions, thumbnails. |
| `lib/storage.mjs` | Revisions, quotas, rebuilding files, garbage collection. |
| `lib/blobs.mjs` | Content-addressed files on disk. |
| `lib/api/*.mjs` | The API: auth, documents (with folders, shares, links, favorites), groups, admin. |
| `dashboard/` | The file browser (plain JavaScript), and `editor-host.js` for the editor page. |
| `tests/` | `node --test` suites; each runs a server in-process on a temp folder. |

**Documents are stored by chunk.** A `.phx` is a container of chunks
(docs/phx-format.md). On upload the server checks the container is whole,
stores each chunk's stored bytes once under its SHA-256, and records the
revision as a manifest of those hashes. A download rebuilds the file from
the manifest exactly as Phoenix wrote it. Revisions and copies share every
chunk they have in common. The server reads only DOCU's stage size and
frame rate, META (the application that wrote it) and THMB (the thumbnail).

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

**Sessions** are random tokens in an HttpOnly cookie, stored hashed.
Every change must come from our origin and carry the session's CSRF token.

## Tests

```sh
cd cloud && node --test "tests/*.test.mjs"
```

`convert.test.mjs` runs only where there is a Phoenix build (and uses
`D:/fla/shape_tweens.swf` and `D:/fla/classic_tween.fla` when they are
there, or `PHOENIX_CLOUD_TEST_SWF` / `_FLA`). `phx.test.mjs` also
round-trips `D:/fla/tutorial1.phx` (or `PHOENIX_CLOUD_TEST_PHX`).
`oauth.test.mjs` runs a mock OpenID provider with its own signing key.
