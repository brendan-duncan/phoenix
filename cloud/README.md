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
your own password until you do. That password is only for `dev`; in
production the first admin's password is `PHOENIX_CLOUD_ADMIN_PASSWORD`,
or a random one the server prints once when it makes the account. Anyone can register an account; what it is
then (ready, waiting for its email address to be confirmed, or waiting for
an admin to unlock it, the default) is an admin setting.

`node cloud/server.mjs import D:/fla/*.fla` adds files to @phoenix's My
Files (or `--to HANDLE`'s), converting them as uploads are.

`dev` runs everything on loopback: the dashboard and API at
`http://localhost:8792`, the editor build at `/editor/` (from
`build-wasm32/webeditor`), and the separate play origin at
`http://127.0.0.1:8793`. Data goes in `cloud/dev-data/` (gitignored);
`node cloud/server.mjs reset` deletes it.

Files dropped on any page of the dashboard upload: into the folder shown
when files can be added there, otherwise into My Files. A dropped folder is
made again in the cloud, folders inside it too, with its documents uploaded
into them; files of other kinds are left out (and listed).

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
| `PHOENIX_CLOUD_URL` | `http://localhost:8792`; the address people use (`serve` needs an `https://` one) |
| `PHOENIX_CLOUD_PLAY_URL` | `http://127.0.0.1:8793`; the play origin (`serve` needs an `https://` one) |
| `PHOENIX_CLOUD_ADMIN_PASSWORD` | the first admin's password on a new production server; random and printed when unset |
| `PHOENIX_CLOUD_PORT`, `_PLAY_PORT`, `_BIND` | `8792`, port + 1, `127.0.0.1` |
| `PHOENIX_CLOUD_DATA` | `cloud/dev-data` (required in production) |
| `PHOENIX_CLOUD_EDITOR` | `build-wasm32/webeditor` |
| `PHOENIX_CLOUD_PHOENIX` | `build/Release/Phoenix.exe` in this repository |
| `PHOENIX_CLOUD_RESOURCES` | `resources` in this repository (templates and tutorials) |
| `PHOENIX_CLOUD_MAX_UPLOAD_MB` | `1024`; any upload |
| `PHOENIX_CLOUD_CONVERT_MAX_UPLOAD_MB` | `200`; a file Phoenix converts |
| `PHOENIX_CLOUD_CONVERT_MAX_EXPANDED_MB` | `2048`; what an FLA's archive or a SWF may unpack to |
| `PHOENIX_CLOUD_CONVERT_MAX_MEMORY_MB` | `2048`; Phoenix's memory for one job |
| `PHOENIX_CLOUD_CONVERSIONS_PER_HOUR` | `30`; documents Phoenix makes for one person (admins: no limit) |
| `PHOENIX_CLOUD_<PROVIDER>_ID`, `_SECRET` | Google and GitHub sign-in |

Admins change the rest while it runs (Admin > Settings): whether people
can register at all (off, only invites and accounts an admin makes let
anyone new in), what a new account is (`admin`: locked until an admin
unlocks it; `email`: locked until its address is confirmed; `open`), the
beta switch, the default storage quota, a banner, and a lock for
maintenance: while locked, only admins can sign in or use the API, the
play origin serves nothing, and everyone else sees the lock message.

## Accounts

- A person has one name: what others see and what they sign in with (or
  their email). Names are unique whatever the capitals: 2 to 30 letters or
  digits, with spaces, `.`, `-`, `_` or `'` between them, never `@`.
  `GET /api/auth/name?name=` says whether one is free as it is typed.
- Passwords are hashed with scrypt.
  Failed sign-ins are counted per name from each address, and per
  address, and slowed down after ten in fifteen minutes (so nobody can
  lock someone else out by guessing at their name).
- Forgot your password? emails a link to choose a new one (good for 2
  hours). Account > Change picture sets a profile picture.
- Notifications (`lib/notify.mjs`): the bell, and the Notifications page,
  where they can be marked read or cleared. Each person chooses, kind by
  kind, whether they show in Phoenix Cloud and whether they come by email
  (a document shared or given to you, being added to a group, a direct
  message, group chat, and for admins accounts waiting and reports); an
  account becoming ready is always told. Nothing comes from people one has
  blocked.
- Messages (`lib/messages.mjs`, `/messages`): direct messages between two
  people, and a chat for each group (and each team, once there are teams),
  fetched every few seconds while the page shows. Who may start a direct
  message with someone is their choice (Account > Messages): anyone,
  people in a group with them (the default), people in their team, only
  people they chose, or no one; the people they chose can always write,
  and anyone they wrote to can answer. Blocking stops messages both ways,
  hides the blocked person's messages in group chats and their
  notifications. Anyone can delete their own messages and report others';
  reports go to Admin > Reports, where a message can be removed. A person
  sends at most 30 messages a minute.
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
  once), sets plans and passwords, suspends, makes admins, sets storage
  quotas and deletes accounts with what they own (groups they own go to
  another member). An admin cannot suspend, demote or delete themself.
- Plans (`lib/plans.mjs`): every account has one: beta or free now, pro and
  enterprise later. New accounts start on Admin > Settings' default plan
  (beta). What each plan allows is not decided: `planLimits()` is where
  limits will come from, by the plan `effectivePlan()` gives.
- Teams are in the database only, for later: a team (`teams`) has an
  owner, a plan and members (`team_members`), and groups can belong to it
  (`groups.team_id`), for a company with a group for each production. A
  member gets the better of their own plan and the team's; a managed member
  gets the team's, and cannot delete their own account.
- Anyone can delete their own account (Account > Delete account), with
  their password (or their name, without one). Their documents and folders
  go with it; first they can give each document others use to one of the
  people who use it, and must give each group they own, with other
  members, to one of them. The only site admin can't.
- Documents can be handed on (Transfer ownership…, `POST
  /api/documents/:id/transfer`) to another person or to a group the owner
  is in; the owner before keeps it as an editor who can download it. A
  document is in one group's files, and shared with any number of other
  groups, each with its own role (a view-only group, say).

## Editing and playing

`/editor/?doc=ID` is the web editor on a cloud document, `&play=1` to play
it, `&tutorial=LESSON` to run a lesson on it. Phoenix starts with
`--hosted`, so New, Open, Save As, templates, Close, the Start Screen and
Tutorials are left out of its menus: new documents, templates and tutorials
start from the dashboard's New document window. The server serves the editor's page with `dashboard/editor-host.js`
added, which fetches the document, puts it in Phoenix's file system and
starts Phoenix on it (with `--play` to play); each Save in Phoenix comes
back to it (docs/web-editor.md, "A page that keeps documents") and is
uploaded as a new version. A save on top of someone else's newer one, or by
someone who can only view, offers to save a copy instead. A bar above
Phoenix shows the document, whether its last save reached the cloud, and
a mark while there are unsaved changes.

- Save (and Ctrl+S anywhere on the page) only ever saves to the cloud;
  File > Download a Copy keeps a copy in the browser's downloads, and
  File > Back to Phoenix Cloud closes the editor, offering to save first.

- The loading screen shows the document's picture and name, and the
  dashboard fetches the editor ahead of time, so it opens quickly.
- Drafts: every two minutes the editor sends unsaved work to the server as
  a draft, kept per person. Opening the document again offers the unsaved
  changes or the last saved version; a save deletes the draft.
- Edit locks: an editor holds the document's lock while it is open (renewed
  every 30 s, let go when the page closes). Cards show who is editing, and
  someone else's save then goes to a copy.
- File > Import > Open Library from Phoenix Cloud opens another cloud
  document's library beside this one.
- `/play/ID` is the player page: the document's picture with a play button,
  then Phoenix's player for people who may open the document, or an HTML5
  version (rendered once a version, served from the play origin with a
  signed, expiring address) for people who may only view it.
- Cards play an animated preview when the pointer is over them, made in
  the background after each save. Versions show their pictures.
- Fonts: the font library (Admin > Fonts; `node server.mjs fonts` gets
  Liberation, Carlito and Caladea, which stand in for Arial, Times New
  Roman, Courier New, Calibri and Cambria with the same widths) is loaded
  by the web editor and used by Phoenix on the server.
- My Files sorts by last edited, name, newest or size; Ctrl-click or the
  check box selects several to move, favorite or trash at once; double-click
  a title to rename it. An empty My Files welcomes new people with lessons
  and templates.

## New documents and conversion

`lib/convert.mjs` runs Phoenix with no window, one job at a time, in a
folder of its own with settings of its own (the person's own Phoenix
settings are never touched), with a time limit:

- a new document: a JSFL script for `Phoenix run-script` makes it at the
  size, frame rate and background asked for;
- a starter template (Phoenix's `resources/templates`, as on its Start
  Screen): the script opens the template's XFL folder and saves a copy,
  with the template's own picture as the thumbnail;
- a tutorial (`resources/tutorials`): its starting document, new at the
  lesson's size or opened from its `start`; the editor then runs the lesson
  on it (`/editor/?doc=ID&tutorial=LESSON`);
- an FLA or SWF upload: the script opens it and saves it as `.phx`;
- an SVG, EPS or AI upload: the script imports it into a new document, the
  stage sized to an SVG's own page or fitted round the artwork;
- then `Phoenix render` draws the first frame 256 pixels wide, and the
  server puts it in the file as its thumbnail (THMB), as Phoenix's own save
  does. A `.phx` uploaded without a thumbnail gets one the same way.

Phoenix reads uploads that people send, so in production it should run as
a user of its own with no access to anything else.

## Keeping memory in hand

- **Uploads go to disk.** A request's body is written to a file under the
  data folder's `uploads/` as it arrives (with the size limit checked as it
  comes) and deleted when the request ends. Storing a `.phx` reads its
  structure in small pieces and streams each chunk into its blob, checking
  its checksum on the way; a download is rebuilt as it is sent, a blob at a
  time. No document is ever whole in the server's memory, whatever its size.
- **One conversion at a time**, at most 20 waiting, 3 minutes each.
- **Before Phoenix opens an upload**: its own size limit, and how large it
  unpacks: an FLA's ZIP central directory (sizes and file count, ZIP64
  too, read without unpacking anything) and a SWF's header.
- **While Phoenix runs**, a watchdog reads its memory (from `/proc` on
  Linux, `tasklist` on Windows, `ps` elsewhere) every 200 ms and stops the
  job past the limit. In production it can also run under a hard limit of
  the system's: `systemd-run --scope -p MemoryMax=…`.
- **Per person**, so many documents made by Phoenix an hour (new, template,
  tutorial, converted upload).

## Running it on a server

`tools/deploy_cloud.py` sets up a Debian or Ubuntu server and deploys to it;
the server, the SSH key and the two names (the service's, and the play
origin's) are in `tools/deploy_cloud.ini`, which the first run writes.

- `setup` (once): Node 22, the libraries Phoenix needs, the user
  `phoenix-cloud`, `/opt/phoenix-cloud` (releases), `/var/lib/phoenix-cloud`
  (data), `/etc/phoenix-cloud.env` (settings, with a random first admin
  password), the systemd service `phoenix-cloud` (with a memory ceiling of
  70% of the server's), a site for Apache or nginx proxying both names to
  ports 8792 and 8793, and a Let's Encrypt certificate for both.
- `deploy` runs the tests, sends `cloud/` and Phoenix's templates and
  tutorials as a new release, and makes it current if the service answers
  with it (otherwise the release before comes back). `--editor` also sends
  `build-wasm32/webeditor`; `--phoenix release` (or a path) also sends
  Phoenix's Linux AppImage, which is unpacked and run with no window for
  new documents and conversions, then checked (`server.mjs check`).
- `status`, `logs [-f]`, `restart`, `rollback`, `backup` (a copy of the
  database, downloaded) and `cli COMMAND` (`server.mjs` there: `fonts`,
  `check`, `invite`, `set-password`, `make-admin`, `gc`).

On the server, `sudo phoenix-cloud-admin` does the same (`tools/cloud/`).

## Running parts on other machines

The service is cut where it can be split later:

| Part | Here | On its own machine |
|---|---|---|
| Web and API | `server.mjs`, `lib/app.mjs` | several behind a load balancer, once the records move off SQLite |
| Records | SQLite through `lib/store.mjs` | PostgreSQL (the SQL is plain; the store is the one place it lives) |
| Document bytes | `lib/blobs.mjs`, a folder | object storage (S3-compatible): `stage`/`commit`/`stream` map onto upload and get |
| Phoenix | `lib/convert.mjs`, a queue in the process | worker machines taking jobs from a queue, files in and out by blob store |
| Mail | `lib/mail.mjs`, an outbox folder | an SMTP relay or mail service |

Jobs already pass only files and small results between the API and the
converter, so moving the converter means swapping the queue and how files
reach it, not the code that uses it.

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
| `lib/starters.mjs` | Phoenix's starter templates and tutorials, read from its resources. |
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

**Downloads.** Downloading (keeping a copy: `/download`, a ZIP of several
from `/api/download?ids=`, Make a copy, the editor's Download a Copy and
Save a copy) is a permission of its own. The owner and the members of the
owning group may; a share to a person or a group may when its Can
download switch is on (on by default for editors); anyone who sees it by
a link or because it is public may when the document allows downloads.
Editors without it still open the document in the web editor
(`/file`), which then leaves out Download a Copy and Export FLA for Other
Apps (`--no-download`).

**Groups.** A group is owned like a person's files: by its creator first,
who can hand it to another member (`groups.owner_id`). The owner is always
an admin; a group can have many admins, and its last admin can neither
leave nor be demoted. Members get the group's default role on its files.
Only the owner deletes a group, and its files become theirs, in a folder
named after the group.

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
