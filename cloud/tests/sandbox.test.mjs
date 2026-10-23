// Phoenix's sandbox for uploaded files: what bubblewrap is asked to show it.
// (The sandbox itself runs on the Linux server; `server.mjs check` tries it.)

import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { phoenixFolders, plainEnv, sandboxArgs } from '../lib/sandbox.mjs';

describe('the sandbox', () => {
  const args = sandboxArgs({
    exe: '/opt/phoenix-cloud/bin/phoenix',
    args: ['run-script', '/var/lib/phoenix-cloud/work/job1/job.jsfl'],
    dir: '/var/lib/phoenix-cloud/work/job1',
    readOnly: ['/opt/phoenix-cloud', '/var/lib/phoenix-cloud/fonts'],
    inputs: ['/var/lib/phoenix-cloud/uploads/abc.fla', '/opt/phoenix-cloud/current/resources/templates/hd/DOMDocument.xml'],
    env: { QT_QPA_PLATFORM: 'offscreen' },
    exists: (p) => p === '/lib64',
    isLink: (p) => ({ '/bin': 'usr/bin', '/lib': 'usr/lib' })[p] ?? null,
  });
  const pairs = (flag) => args.flatMap((a, i) => (a === flag ? [`${args[i + 1]}`] : []));

  it('has no network, no other processes, and none of the service\'s environment', () => {
    for (const flag of ['--unshare-all', '--die-with-parent', '--new-session', '--clearenv']) {
      assert.ok(args.includes(flag), flag);
    }
    assert.deepEqual(args.slice(args.indexOf('--setenv'), args.indexOf('--setenv') + 3), ['--setenv', 'QT_QPA_PLATFORM', 'offscreen']);
  });

  it('writes only in the job\'s folder, and reads only what it needs', () => {
    assert.deepEqual(pairs('--bind'), ['/var/lib/phoenix-cloud/work/job1']);
    assert.deepEqual(pairs('--ro-bind'), [
      '/usr', '/lib64', '/opt/phoenix-cloud', '/var/lib/phoenix-cloud/fonts',
      '/var/lib/phoenix-cloud/uploads/abc.fla', '/opt/phoenix-cloud/current/resources/templates/hd',
    ]);
    // Never the data folder as a whole, nor the settings.
    assert.ok(!args.includes('/var/lib/phoenix-cloud') && !args.includes('/etc/phoenix-cloud.env'));
    assert.deepEqual(args.slice(args.indexOf('--symlink'), args.indexOf('--symlink') + 3), ['--symlink', 'usr/bin', '/bin']);
    assert.deepEqual(args.slice(args.indexOf('--')), ['--', '/opt/phoenix-cloud/bin/phoenix', 'run-script', '/var/lib/phoenix-cloud/work/job1/job.jsfl']);
  });

  it('shows Phoenix the folder above its own, and keeps the service\'s settings from it', () => {
    assert.deepEqual(phoenixFolders('/nowhere/bin/phoenix'), ['/nowhere']);
    assert.deepEqual(phoenixFolders('/x/bin/phoenix', '/a:/b'), ['/a', '/b']);
    assert.deepEqual(Object.keys(plainEnv({ PATH: '/usr/bin', PHOENIX_CLOUD_SMTP_PASSWORD: 'secret', HOME: '/h' })), ['PATH', 'HOME']);
  });
});
