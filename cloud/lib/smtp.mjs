// Sending one email over SMTP, as Amazon SES (or any mail service) takes
// it: a connection to port 587 made private with STARTTLS (or to 465,
// private from the start), AUTH with the service's SMTP user, then the
// message. No dependencies; one connection per email, which is plenty for
// account and notification mail.

import crypto from 'node:crypto';
import net from 'node:net';
import os from 'node:os';
import tls from 'node:tls';

const TIMEOUT_MS = 30 * 1000;

export class SmtpError extends Error {
  constructor(message, code = 0) {
    super(message);
    // The reply code: 4xx is worth trying again later, 5xx is not.
    this.code = code;
  }

  get temporary() {
    return this.code === 0 || (this.code >= 400 && this.code < 500);
  }
}

/** `text` for a header: as it is when plain ASCII, else RFC 2047 UTF-8. */
function headerText(text) {
  const s = String(text).replace(/[\r\n]+/g, ' ');
  return /^[\x20-\x7e]*$/.test(s) ? s : `=?UTF-8?B?${Buffer.from(s, 'utf8').toString('base64')}?=`;
}

/** The address in "Name <address>" (or the whole of it). */
export function addressOf(mailbox) {
  const m = /<([^<>\s]+@[^<>\s]+)>\s*$/.exec(String(mailbox));
  return (m ? m[1] : String(mailbox)).trim();
}

/** "Name <address>" with the name encoded when it needs to be. */
function mailboxHeader(mailbox) {
  const m = /^\s*(.*?)\s*<([^<>\s]+@[^<>\s]+)>\s*$/.exec(String(mailbox));
  if (!m || !m[1]) {
    return addressOf(mailbox);
  }
  const name = m[1].replace(/^"(.*)"$/, '$1');
  const shown = /^[\x20-\x7e]*$/.test(name) ? `"${name.replace(/["\\]/g, '')}"` : headerText(name);
  return `${shown} <${m[2]}>`;
}

/** The whole message, headers and a base64 UTF-8 text body, with CRLF line ends. */
export function buildMessage({ from, to, replyTo = null, subject, text, date = new Date() }) {
  const domain = addressOf(from).split('@')[1] || 'localhost';
  const body = Buffer.from(String(text).replace(/\r?\n/g, '\r\n'), 'utf8').toString('base64').replace(/.{1,76}/g, '$&\r\n');
  const headers = [
    `From: ${mailboxHeader(from)}`,
    `To: ${mailboxHeader(to)}`,
    replyTo ? `Reply-To: ${mailboxHeader(replyTo)}` : null,
    `Subject: ${headerText(subject)}`,
    `Date: ${date.toUTCString().replace('GMT', '+0000')}`,
    `Message-ID: <${crypto.randomUUID()}@${domain}>`,
    'MIME-Version: 1.0',
    'Content-Type: text/plain; charset=UTF-8',
    'Content-Transfer-Encoding: base64',
    // Mail a program sends: auto-replies should not answer it.
    'Auto-Submitted: auto-generated',
  ].filter(Boolean);
  return `${headers.join('\r\n')}\r\n\r\n${body}`;
}

/** Reads SMTP replies (one or more lines, "250-..." until "250 ...") from a socket. */
class Replies {
  constructor(socket) {
    this.attach(socket);
  }

  attach(socket) {
    this.socket = socket;
    this.buffer = '';
    this.waiting = null;
    this.failed = null;
    socket.setEncoding('utf8');
    socket.on('data', (chunk) => {
      this.buffer += chunk;
      this.check();
    });
    const fail = (err) => {
      this.failed = err;
      this.waiting?.reject(err);
      this.waiting = null;
    };
    socket.on('error', (err) => fail(new SmtpError(`The mail server could not be reached: ${err.message}`)));
    socket.on('close', () => fail(new SmtpError('The mail server closed the connection.')));
    socket.setTimeout(TIMEOUT_MS, () => {
      fail(new SmtpError('The mail server took too long to answer.'));
      socket.destroy();
    });
  }

  /** Stops reading this socket (before STARTTLS wraps it). */
  detach() {
    this.socket.removeAllListeners('data');
    this.socket.removeAllListeners('error');
    this.socket.removeAllListeners('close');
    this.socket.setTimeout(0);
  }

  check() {
    if (!this.waiting) {
      return;
    }
    const lines = this.buffer.split('\r\n');
    for (let i = 0; i < lines.length - 1; i++) {
      if (/^\d{3} /.test(lines[i]) || /^\d{3}$/.test(lines[i])) {
        const reply = lines.slice(0, i + 1);
        this.buffer = lines.slice(i + 1).join('\r\n');
        const { resolve } = this.waiting;
        this.waiting = null;
        resolve({ code: Number(reply[i].slice(0, 3)), lines: reply.map((l) => l.slice(4)) });
        return;
      }
    }
  }

  next() {
    if (this.failed) {
      return Promise.reject(this.failed);
    }
    return new Promise((resolve, reject) => {
      this.waiting = { resolve, reject };
      this.check();
    });
  }
}

function connect(options, secure) {
  return new Promise((resolve, reject) => {
    const socket = secure
      ? tls.connect({ host: options.host, port: options.port, servername: options.host })
      : net.connect({ host: options.host, port: options.port });
    socket.once(secure ? 'secureConnect' : 'connect', () => resolve(socket));
    socket.once('error', (err) => reject(new SmtpError(`The mail server could not be reached: ${err.message}`)));
    socket.setTimeout(TIMEOUT_MS, () => {
      socket.destroy();
      reject(new SmtpError('The mail server took too long to answer.'));
    });
  });
}

/**
 * Sends `message` (buildMessage's) from `from` to `to` through the server in
 * `options`: { host, port, user, password, tls: 'starttls' | 'tls' | 'none' }.
 * 'none' sends in the clear, for a relay on the same machine (and tests).
 */
export async function sendMail(options, { from, to, message }) {
  const mode = options.tls ?? 'starttls';
  let socket = await connect(options, mode === 'tls');
  const replies = new Replies(socket);
  const write = (line) => socket.write(`${line}\r\n`);
  const expect = async (codes, what) => {
    const reply = await replies.next();
    if (!codes.includes(reply.code)) {
      throw new SmtpError(`The mail server refused ${what}: ${reply.code} ${reply.lines.join(' ')}`, reply.code);
    }
    return reply;
  };
  const name = os.hostname() || 'localhost';
  try {
    await expect([220], 'the connection');
    write(`EHLO ${name}`);
    let hello = await expect([250], 'EHLO');
    if (mode === 'starttls') {
      if (!hello.lines.some((l) => /^STARTTLS\b/i.test(l))) {
        throw new SmtpError('The mail server does not offer STARTTLS, so the password would go in the clear.', 554);
      }
      write('STARTTLS');
      await expect([220], 'STARTTLS');
      replies.detach();
      socket = await new Promise((resolve, reject) => {
        const secure = tls.connect({ socket, servername: options.host }, () => resolve(secure));
        secure.once('error', (err) => reject(new SmtpError(`TLS with the mail server failed: ${err.message}`)));
      });
      replies.attach(socket);
      write(`EHLO ${name}`);
      hello = await expect([250], 'EHLO');
    }
    if (options.user) {
      const plain = Buffer.from(`\0${options.user}\0${options.password ?? ''}`, 'utf8').toString('base64');
      write(`AUTH PLAIN ${plain}`);
      await expect([235], 'the SMTP user and password');
    }
    write(`MAIL FROM:<${addressOf(from)}>`);
    await expect([250], `the sender ${addressOf(from)}`);
    write(`RCPT TO:<${addressOf(to)}>`);
    await expect([250, 251], `the recipient ${addressOf(to)}`);
    write('DATA');
    await expect([354], 'the message');
    // A line that starts with a dot gets another (RFC 5321, 4.5.2).
    socket.write(`${message.replace(/^\./gm, '..')}\r\n.\r\n`);
    const done = await expect([250], 'the message');
    write('QUIT');
    // Its goodbye, before the connection closes; the message is sent either way.
    await replies.next().catch(() => {});
    return { reply: done.lines.join(' ') };
  } finally {
    socket.end();
    socket.destroy();
  }
}
