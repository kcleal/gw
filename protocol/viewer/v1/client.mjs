// Framework-independent browser adapter. No URLs, DOM nodes, GLFW or WASM globals.
// transport: { exchange(message): Promise<object>, frame(identity): Promise<Uint8Array> }
export const protocol = 'gw-viewer';
export const version = 1;

export class ViewerError extends Error {
  constructor(code, message) { super(message); this.code = code; }
}

export function framebuffer(viewport) {
  const {width, height, pixel_ratio: ratio} = viewport;
  const w = Math.floor(width * ratio + 0.5), h = Math.floor(height * ratio + 0.5);
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 ||
      width > 8192 || height > 8192 || !Number.isFinite(ratio) || ratio < .25 || ratio > 8 ||
      w < 200 || h < 100 || w > 8192 || h > 8192 || w * h > 33554432)
    throw new ViewerError('invalid_viewport', 'Unsupported viewport/framebuffer size');
  return {width: w, height: h};
}

function counter(value, zero = false) {
  if (typeof value !== 'string' || !/^(0|[1-9][0-9]{0,19})$/.test(value) ||
      (!zero && value === '0') || BigInt(value) > 18446744073709551615n)
    throw new ViewerError('invalid_response', 'Invalid revision or sequence');
  return BigInt(value);
}

export function readReply(reply, requestId) {
  if (!reply || reply.protocol !== protocol || reply.version !== version || reply.request_id !== requestId)
    throw new ViewerError('invalid_response', 'Protocol version or request ID mismatch');
  if (reply.type === 'error')
    throw new ViewerError(reply.error?.code || 'invalid_response', reply.error?.message || 'Server error');
  if (!['welcome', 'state', 'ack'].includes(reply.type))
    throw new ViewerError('invalid_response', 'Unknown response type');
  if (reply.type === 'ack') counter(reply.revision);
  else {
    const view = reply.view, f = view?.frame;
    counter(view?.revision);
    if (!view.shared || typeof view.shared !== 'object' || Array.isArray(view.shared) ||
        !view.client || typeof view.client !== 'object' || Array.isArray(view.client) ||
        !f || typeof f.frame_id !== 'string' || f.revision !== view.revision || f.mime_type !== 'image/png')
      throw new ViewerError('invalid_response', 'Invalid view/frame');
    const size = framebuffer(f.viewport);
    if (f.width !== size.width || f.height !== size.height)
      throw new ViewerError('invalid_response', 'Frame dimensions disagree with viewport');
  }
  if (reply.type === 'welcome') {
    for (const key of ['session_id', 'client_id', 'connection_id', 'resume_token'])
      if (typeof reply[key] !== 'string' || !reply[key])
        throw new ViewerError('invalid_response', `Missing ${key}`);
    counter(reply.last_sequence, true);
    if (!Array.isArray(reply.capabilities) || !Number.isFinite(reply.heartbeat_seconds) || reply.heartbeat_seconds <= 0)
      throw new ViewerError('invalid_response', 'Invalid welcome');
  }
  return reply;
}

function pngSize(bytes) {
  const signature = [137, 80, 78, 71, 13, 10, 26, 10];
  if (!(bytes instanceof Uint8Array) || bytes.length < 24 || signature.some((v, i) => bytes[i] !== v) ||
      String.fromCharCode(...bytes.subarray(12, 16)) !== 'IHDR')
    throw new ViewerError('invalid_frame', 'Expected binary PNG');
  const d = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {width: d.getUint32(16), height: d.getUint32(20)};
}

export class ViewerClient {
  constructor(transport, {onView = () => {}, onRevision = () => {}} = {}) {
    if (typeof transport.exchange !== 'function' || typeof transport.frame !== 'function')
      throw new TypeError('Transport must implement exchange and frame');
    this.transport = transport;
    this.onView = onView; this.onRevision = onRevision;
    this.identity = null; this.view = null; this.sequence = 0n;
    this.queue = Promise.resolve(); this.pending = null; this.epoch = 0;
  }

  _enqueue(fn) {
    const result = this.queue.then(fn);
    this.queue = result.catch(() => {});
    return result;
  }

  _envelope(type, values) {
    return {protocol, version, type, request_id: crypto.randomUUID(), ...values};
  }

  join(sessionId, viewport, resume = null) {
    framebuffer(viewport);
    return this._enqueue(async () => {
      this.epoch++; this.pending = null; this.view = null;
      // Join is not automatically retried: a lost new-client welcome has no resume credentials.
      const message = this._envelope('join', {session_id: sessionId, viewport, ...(resume || {})});
      const reply = readReply(await this.transport.exchange(message), message.request_id);
      if (reply.type !== 'welcome' || reply.session_id !== sessionId)
        throw new ViewerError('invalid_response', 'Expected welcome for requested session');
      this.identity = Object.fromEntries(['session_id', 'client_id', 'connection_id', 'resume_token'].map(k => [k, reply[k]]));
      this.sequence = counter(reply.last_sequence, true);
      this.capabilities = reply.capabilities;
      this.heartbeatSeconds = reply.heartbeat_seconds;
      await this._present(reply.view);
      return reply;
    });
  }

  get resumeCredentials() {
    return this.identity && {client_id: this.identity.client_id, resume_token: this.identity.resume_token};
  }

  _request(type, values = {}) {
    values = structuredClone(values);
    return this._enqueue(async () => {
      if (!this.identity) throw new ViewerError('not_joined', 'Join first');
      if (this.pending) throw new ViewerError('uncertain_request', 'Retry the pending request or reconnect before continuing');
      const {resume_token, ...identity} = this.identity;
      counter(String(this.sequence + 1n));
      const message = this._envelope(type, {...identity, sequence: String(++this.sequence), ...values});
      // Capture action revision/layout at call time, not after another frame arrives.
      this.pending = message;
      return this._exchange(message);
    });
  }

  async _exchange(message) {
    // A transport rejection is ambiguous. Retain the exact envelope for retry().
    const raw = await this.transport.exchange(message);
    let reply;
    try { reply = readReply(raw, message.request_id); }
    catch (e) {
      if (raw?.protocol === protocol && raw.version === version && raw.request_id === message.request_id && raw.type === 'error')
        this.pending = null;
      throw e;
    }
    this.pending = null;
    if (reply.view) await this._present(reply.view);
    else if (this.view && counter(reply.revision) > counter(this.view.revision)) this.onRevision(reply.revision);
    if (message.type === 'leave') { this.identity = null; this.view = null; this.epoch++; }
    return reply;
  }

  retry() {
    return this._enqueue(() => {
      if (!this.pending) throw new ViewerError('no_pending_request', 'Nothing to retry');
      return this._exchange(this.pending);
    });
  }

  async _present(view) {
    const epoch = this.epoch, identity = this.identity;
    const bytes = await this.transport.frame({session_id: identity.session_id, client_id: identity.client_id,
      connection_id: identity.connection_id, frame_id: view.frame.frame_id});
    const size = pngSize(bytes);
    if (size.width !== view.frame.width || size.height !== view.frame.height)
      throw new ViewerError('invalid_frame', 'PNG dimensions disagree with frame metadata');
    if (epoch !== this.epoch) return;
    // Consumer must decode/display PNG and its layout atomically before resolving.
    await this.onView(view, bytes);
    this.view = view;
  }

  action(action) {
    if (!this.view) return Promise.reject(new ViewerError('no_frame', 'Wait for a displayed frame'));
    const values = {expected_revision: this.view.revision, action};
    if (['pointer', 'key', 'vscroll', 'track_resize'].includes(action.type)) values.frame_id = this.view.frame.frame_id;
    return this._request('action', values);
  }

  resize(viewport) { framebuffer(viewport); return this._request('viewport', {viewport}); }
  snapshot() { return this._request('snapshot'); }
  heartbeat() { return this._request('heartbeat'); }
  leave() { return this._request('leave'); }
}

// Optional HTTP binding. Authentication, origins and URLs belong to the host app.
// Frame identity is POSTed so reconnect credentials do not appear in URLs/logs.
export function httpTransport({messageUrl, frameUrl, fetch: fetcher = globalThis.fetch,
  headers = () => ({}), credentials = 'same-origin'}) {
  async function post(url, data) {
    return fetcher(url, {method: 'POST', credentials, cache: 'no-store',
      headers: {'Content-Type': 'application/json', ...await headers()}, body: JSON.stringify(data)});
  }
  return {
    async exchange(message) {
      const response = await post(messageUrl, message);
      // Structured protocol errors may use an HTTP error status.
      if (!(response.headers.get('Content-Type') || '').startsWith('application/json'))
        throw new ViewerError('transport_error', `Expected JSON response (${response.status})`);
      return response.json();
    },
    async frame(identity) {
      const response = await post(frameUrl, identity);
      if (!response.ok || !(response.headers.get('Content-Type') || '').startsWith('image/png')) {
        let error;
        try { error = (await response.json()).error; } catch {}
        throw new ViewerError(error?.code || 'transport_error', error?.message || `Frame request failed (${response.status})`);
      }
      return new Uint8Array(await response.arrayBuffer());
    }
  };
}
