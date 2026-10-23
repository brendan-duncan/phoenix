// Email through an SMTP service: what the message looks like, the
// conversation with the service, refusing to send a password in the clear,
// and Admin > Email's test email.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { after, before, describe, it } from 'node:test';
import { Mailer } from '../lib/mail.mjs';
import { buildMessage, sendMail } from '../lib/smtp.mjs';
import { startServer } from './helpers.mjs';

/** A stand-in mail service: { port, received, close() }. */
async function fakeSmtp({ starttls = false, refuseRecipient = false } = {}) {
  const received = [];
  const server = net.createServer((socket) => {
    let data = null;
    let buffer = '';
    const mail = { commands: [] };
    socket.on('error', () => {});
    socket.write('220 fake.example ESMTP\r\n');
    socket.on('data', (chunk) => {
      buffer += chunk.toString('utf8');
      let at;
      while ((at = buffer.indexOf('\r\n')) >= 0) {
        const line = buffer.slice(0, at);
        buffer = buffer.slice(at + 2);
        if (data !== null) {
          if (line === '.') {
            mail.data = data.join('\r\n');
            received.push(mail);
            data = null;
            socket.write('250 Ok 0101-queued\r\n');
          } else {
            data.push(line);
          }
          continue;
        }
        mail.commands.push(line);
        const verb = line.split(' ')[0].toUpperCase();
        if (verb === 'EHLO') {
          socket.write(`250-fake.example\r\n${starttls ? '250-STARTTLS\r\n' : ''}250 AUTH PLAIN LOGIN\r\n`);
        } else if (verb === 'AUTH') {
          const [, user, password] = Buffer.from(line.split(' ')[2], 'base64').toString('utf8').split('\0');
          socket.write(user === 'ses-user' && password === 'ses-secret' ? '235 Authentication successful\r\n' : '535 Authentication Credentials Invalid\r\n');
        } else if (verb === 'MAIL') {
          socket.write('250 Ok\r\n');
        } else if (verb === 'RCPT') {
          socket.write(refuseRecipient ? '550 Mailbox unavailable\r\n' : '250 Ok\r\n');
        } else if (verb === 'DATA') {
          data = [];
          socket.write('354 End data with <CR><LF>.<CR><LF>\r\n');
        } else if (verb === 'QUIT') {
          socket.end('221 Bye\r\n');
        } else {
          socket.write('502 Unknown\r\n');
        }
      }
    });
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  return { port: server.address().port, received, close: () => new Promise((resolve) => server.close(resolve)) };
}

describe('SMTP', () => {
  it('builds a message with its headers and a UTF-8 body', () => {
    const message = buildMessage({ from: 'Phoenix Cloud <no-reply@phoenixanimate.com>', to: 'ann@example.com', replyTo: 'me@example.com', subject: 'Prêt à animer', text: 'Hello\n.\nBye' });
    const [head, body] = message.split('\r\n\r\n');
    assert.match(head, /^From: "Phoenix Cloud" <no-reply@phoenixanimate.com>$/m);
    assert.match(head, /^Reply-To: me@example.com$/m);
    assert.match(head, /^Subject: =\?UTF-8\?B\?/m);
    assert.match(head, /^Message-ID: <[0-9a-f-]+@phoenixanimate.com>$/m);
    assert.equal(Buffer.from(body.replace(/\r\n/g, ''), 'base64').toString('utf8'), 'Hello\r\n.\r\nBye');
  });

  it('talks to the service: AUTH, the sender, the recipient, the message', async () => {
    const smtp = await fakeSmtp();
    const message = buildMessage({ from: 'no-reply@phoenixanimate.com', to: 'ann@example.com', subject: 'Hi', text: 'Hello' });
    const result = await sendMail({ host: '127.0.0.1', port: smtp.port, user: 'ses-user', password: 'ses-secret', tls: 'none' },
      { from: 'Phoenix Cloud <no-reply@phoenixanimate.com>', to: 'ann@example.com', message });
    assert.equal(result.reply, 'Ok 0101-queued');
    assert.deepEqual(smtp.received[0].commands.filter((c) => /^(MAIL|RCPT)/.test(c)), ['MAIL FROM:<no-reply@phoenixanimate.com>', 'RCPT TO:<ann@example.com>']);
    await assert.rejects(sendMail({ host: '127.0.0.1', port: smtp.port, user: 'ses-user', password: 'wrong', tls: 'none' }, { from: 'a@b.c', to: 'd@e.f', message }),
      (err) => err.code === 535 && !err.temporary);
    await smtp.close();
  });

  it('will not send the password where STARTTLS is not offered', async () => {
    const smtp = await fakeSmtp();
    await assert.rejects(sendMail({ host: '127.0.0.1', port: smtp.port, user: 'ses-user', password: 'ses-secret' }, { from: 'a@b.c', to: 'd@e.f', message: 'x' }),
      /does not offer STARTTLS/);
    assert.ok(!smtp.received.length && true);
    await smtp.close();
  });
});

describe('hourly email budgets', () => {
  it('hold mail past so many to one address, and so many in all', async () => {
    const smtp = await fakeSmtp();
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'phoenix-mail-'));
    const mailer = new Mailer({
      dataDir: dir, mode: 'production', mailFrom: 'no-reply@phoenixanimate.com', mailPerHour: 3, mailPerAddressPerHour: 2,
      smtp: { host: '127.0.0.1', port: smtp.port, user: 'ses-user', password: 'ses-secret', tls: 'none' },
    });
    const send = (to) => mailer.send({ to, subject: 'Hi', text: 'Hello' });
    assert.equal((await send('a@example.com')).status, 'sent');
    assert.equal((await send('a@example.com')).status, 'sent');
    const third = await send('a@example.com');
    assert.equal(third.status, 'held');
    assert.match(third.detail, /too many emails to this address/);
    assert.equal((await send('b@example.com')).status, 'sent');
    assert.match((await send('c@example.com')).detail, /limit of 3 emails an hour/);
    assert.equal(smtp.received.length, 3);
    assert.ok(mailer.recent().some((m) => m.status.startsWith('held')));
    await smtp.close();
    fs.rmSync(dir, { recursive: true, force: true });
  });
});

describe('the outbox', () => {
  it('finds, deletes, and lets old emails go', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'phoenix-outbox-'));
    const mailer = new Mailer({ dataDir: dir, mode: 'production', mailFrom: 'no-reply@phoenixanimate.com', mailKeepDays: 30 });
    await mailer.send({ to: 'ann@example.com', subject: 'Confirm your account', text: 'x' });
    await new Promise((resolve) => setTimeout(resolve, 5));
    await mailer.send({ to: 'bo@example.com', subject: 'A test', text: 'y' });
    assert.deepEqual(mailer.find({ q: 'ANN' }).messages.map((m) => m.to), ['ann@example.com']);
    assert.deepEqual(mailer.find({ status: 'outbox' }).total, 2);
    assert.equal(mailer.find({ status: 'sent' }).total, 0);
    const [latest] = mailer.find().messages;
    assert.ok(Math.abs(latest.at - Date.now()) < 60000);
    assert.equal(mailer.remove(latest.file), true);
    assert.equal(mailer.remove('../../etc/passwd'), false);
    // One from 40 days ago goes when the outbox is pruned.
    const old = new Date(Date.now() - 40 * 24 * 3600 * 1000).toISOString().replace(/[:.]/g, '-');
    fs.writeFileSync(path.join(dir, 'outbox', `${old}-old@example.com.txt`), 'To: old@example.com\nSubject: Old\nStatus: sent (ok)\n\nold\n');
    assert.equal(mailer.find().total, 2);
    assert.equal(mailer.prune(), 1);
    assert.equal(mailer.removeAll({ q: 'confirm' }), 1);
    assert.equal(mailer.find().total, 0);
    fs.rmSync(dir, { recursive: true, force: true });
  });
});

describe('Admin > Email', () => {
  let smtp;
  let server;
  let admin;

  before(async () => {
    smtp = await fakeSmtp();
    server = await startServer({
      env: {
        PHOENIX_CLOUD_SMTP_HOST: '127.0.0.1', PHOENIX_CLOUD_SMTP_PORT: String(smtp.port), PHOENIX_CLOUD_SMTP_TLS: 'none',
        PHOENIX_CLOUD_SMTP_USER: 'ses-user', PHOENIX_CLOUD_SMTP_PASSWORD: 'ses-secret',
        PHOENIX_CLOUD_MAIL_FROM: 'Phoenix Cloud <no-reply@phoenixanimate.com>', PHOENIX_CLOUD_MAIL_REPLY_TO: 'me@example.com',
      },
    });
    admin = server.client();
    await admin.signInWith('phoenix', 'animate');
  });

  after(async () => {
    await server.close();
    await smtp.close();
  });

  it('sends a test email and says how it went', async () => {
    const { mail } = await admin.get('/api/admin/outbox');
    assert.deepEqual(mail, { sending: true, host: '127.0.0.1', from: 'Phoenix Cloud <no-reply@phoenixanimate.com>', replyTo: 'me@example.com', perHour: 100, keepDays: 30 });
    const result = await admin.post('/api/admin/email/test', { to: 'ann@example.com' });
    assert.deepEqual(result, { status: 'sent', detail: 'Ok 0101-queued' });
    assert.match(smtp.received.at(-1).data, /^Subject: Phoenix Cloud test email$/m);
    const { messages } = await admin.get('/api/admin/outbox');
    assert.equal(messages[0].status, 'sent (Ok 0101-queued)');
    assert.equal(await admin.fails('POST', '/api/admin/email/test', 400, { json: { to: 'not an address' } }), 'bad_request');
    // Found by address and status; deleted by the same.
    assert.equal((await admin.get('/api/admin/outbox?q=ann@example&status=sent')).total, 1);
    assert.equal((await admin.del('/api/admin/outbox?q=ann@example')).deleted, 1);
    assert.equal((await admin.get('/api/admin/outbox?q=ann@example')).total, 0);
  });

  /** The mail to `to` once it has arrived (sending runs in the background): its decoded body and headers. */
  async function mailTo(to, subject) {
    for (let i = 0; i < 100; i++) {
      const found = smtp.received.find((m) => m.commands.includes(`RCPT TO:<${to}>`) && (!subject || subject.test(m.data)));
      if (found) {
        const [head, body] = found.data.split('\r\n\r\n');
        return { head, text: Buffer.from(body.replace(/\r\n/g, ''), 'base64').toString('utf8') };
      }
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    throw new Error(`No mail to ${to}`);
  }

  it('confirms a new account\'s address when the setting asks for it', async () => {
    await admin.patch('/api/admin/settings', { newAccounts: 'email' });
    const anon = server.client();
    await anon.ok('POST', '/api/auth/register', { json: { name: 'dora', email: 'dora@example.com', password: 'a long password' } });
    const { text } = await mailTo('dora@example.com', /^Subject: Confirm your Phoenix Cloud account$/m);
    const token = /\/confirm\/([A-Za-z0-9_-]+)/.exec(text)[1];
    assert.equal((await anon.post('/api/auth/confirm', { token })).status, 'active');
  });

  it('tells admins by email when someone asks for an account', async () => {
    await admin.patch('/api/me', { email: 'admin@example.com' });
    // A changed address gets a link to confirm it.
    assert.match((await mailTo('admin@example.com', /new email address/)).text, /\/confirm\//);
    await admin.patch('/api/admin/settings', { newAccounts: 'admin' });
    await server.client().ok('POST', '/api/auth/register', { json: { name: 'eli', email: 'eli@example.com', password: 'a long password', message: 'I animate.' } });
    assert.match((await mailTo('admin@example.com', /asked for an account/)).text, /\/admin/);
    // Another within the hour is in the dashboard, not another email.
    await server.client().ok('POST', '/api/auth/register', { json: { name: 'fay', email: 'fay@example.com', password: 'a long password', message: 'Me too.' } });
    await new Promise((resolve) => setTimeout(resolve, 200));
    assert.equal(smtp.received.filter((m) => m.commands.includes('RCPT TO:<admin@example.com>') && /asked for an account/.test(m.data)).length, 1);
    assert.equal((await admin.get('/api/notifications')).notifications.filter((n) => n.kind === 'waiting').length, 2);
  });

  it('emails invites made for an address', async () => {
    const made = await admin.post('/api/admin/invites', { email: 'finn@example.com', note: 'For the storyboard crew' });
    assert.equal(made.emailed.status, 'sent');
    const { head, text } = await mailTo('finn@example.com');
    assert.match(head, /^Subject: phoenix invited you to Phoenix Cloud$/m);
    assert.match(head, /^Reply-To: admin@example.com$/m);
    assert.ok(text.includes(made.url) && text.includes('For the storyboard crew'));
    assert.equal((await admin.post('/api/admin/invites', { email: 'gus@example.com', send: false })).emailed, null);
  });

  it('lets an admin write to someone', async () => {
    const { user } = await admin.post('/api/admin/users', { name: 'hana', email: 'hana@example.com', password: 'a long password' });
    const sent = await admin.post(`/api/admin/users/${user.id}/email`, { subject: 'Your storage', message: 'You are close to your quota.' });
    assert.equal(sent.status, 'sent');
    const { head, text } = await mailTo('hana@example.com', /^Subject: Your storage$/m);
    assert.match(head, /^Reply-To: admin@example.com$/m);
    assert.match(text, /^Hello hana,\r\n\r\nYou are close to your quota\./);
    assert.equal(await admin.fails('POST', `/api/admin/users/${user.id}/email`, 400, { json: { subject: '', message: 'x' } }), 'bad_request');
  });
});
