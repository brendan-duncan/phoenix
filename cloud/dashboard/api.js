// Talking to the server: JSON in and out, the session's CSRF token on every
// change, and a share link's token while a page opened by link is showing.

let csrf = null;
let link = null;

export function setCsrf(token) {
  csrf = token;
}

export function setLink(token) {
  link = token;
}

export class ApiError extends Error {
  constructor(status, code, message) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

function headers(method, extra = {}) {
  const h = { ...extra };
  if (csrf && method !== 'GET') {
    h['X-CSRF-Token'] = csrf;
  }
  if (link) {
    h['X-Phoenix-Link'] = link;
  }
  return h;
}

async function failure(res) {
  let body = {};
  try {
    body = await res.json();
  } catch {
    body = {};
  }
  return new ApiError(res.status, body.error ?? 'error', body.message ?? `The server answered ${res.status}.`);
}

export async function api(method, path, body) {
  const init = { method, headers: headers(method), credentials: 'same-origin' };
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }
  const res = await fetch(path, init);
  if (!res.ok) {
    throw await failure(res);
  }
  return res.status === 204 ? null : res.json();
}

export const get = (path) => api('GET', path);
export const post = (path, body = {}) => api('POST', path, body);
export const patch = (path, body = {}) => api('PATCH', path, body);
export const put = (path, body) => api('PUT', path, body);
export const del = (path) => api('DELETE', path);

/** Sends a file as the body, reporting progress (0 to 1). */
export function send(method, path, file, onProgress = () => {}) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open(method, path);
    for (const [k, v] of Object.entries(headers(method, { 'Content-Type': 'application/octet-stream' }))) {
      xhr.setRequestHeader(k, v);
    }
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        onProgress(e.loaded / e.total);
      }
    };
    xhr.onload = () => {
      let body = {};
      try {
        body = JSON.parse(xhr.responseText);
      } catch {
        body = {};
      }
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(body);
      } else {
        reject(new ApiError(xhr.status, body.error ?? 'error', body.message ?? `The server answered ${xhr.status}.`));
      }
    };
    xhr.onerror = () => reject(new ApiError(0, 'network', 'Could not reach the server.'));
    xhr.send(file);
  });
}
