// Email through an SMTP service: what the message looks like, the
// conversation with the service, refusing to send a password in the clear,
// and Admin > Email's test email.

import assert from 'node:assert/strict';
import net from 'node:net';
import { after, before, describe, it } from 'node:test';
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
    assert.deepEqual(mail, { sending: true, host: '127.0.0.1', from: 'Phoenix Cloud <no-reply@phoenixanimate.com>', replyTo: 'me@example.com' });
    const result = await admin.post('/api/admin/email/test', { to: 'ann@example.com' });
    assert.deepEqual(result, { status: 'sent', detail: 'Ok 0101-queued' });
    assert.match(smtp.received.at(-1).data, /^Subject: Phoenix Cloud test email$/m);
    const { messages } = await admin.get('/api/admin/outbox');
    assert.equal(messages[0].status, 'sent (Ok 0101-queued)');
    assert.equal(await admin.fails('POST', '/api/admin/email/test', 400, { json: { to: 'not an address' } }), 'bad_request');
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
