import assert from 'node:assert/strict';
import {ViewerClient, framebuffer} from '../deps/protocol/viewer/v1/client.mjs';

function png(width, height) {
  const bytes = new Uint8Array(24);
  bytes.set([137, 80, 78, 71, 13, 10, 26, 10]); bytes.set([73, 72, 68, 82], 12);
  const d = new DataView(bytes.buffer); d.setUint32(16, width); d.setUint32(20, height);
  return bytes;
}

let revision = '1', vp = {width: 600, height: 300, pixel_ratio: 1}, frameNo = 0;
let loseReply = false, applied = 0;
const seen = new Map(), frames = new Map(), requests = [];
const transport = {
  async exchange(request) {
    requests.push(structuredClone(request));
    if (seen.has(request.sequence)) return seen.get(request.sequence);
    let reply = {protocol: 'gw-viewer', version: 1, request_id: request.request_id};
    if (request.type === 'heartbeat' || request.type === 'leave') reply = {...reply, type: 'ack', revision};
    else {
      if (request.type === 'action') { applied++; revision = String(BigInt(revision) + 1n); }
      if (request.viewport) vp = request.viewport;
      const size = framebuffer(vp), fid = `f-${++frameNo}`;
      frames.set(fid, png(size.width, size.height));
      const view = {revision, shared: {}, client: {}, frame: {frame_id: fid, revision, viewport: vp,
        ...size, mime_type: 'image/png', layout: {}}};
      reply = {...reply, type: 'state', view};
      if (request.type === 'join') reply = {...reply, type: 'welcome', session_id: 'demo', client_id: 'c',
        connection_id: `connection-${frameNo}`, resume_token: 'token', last_sequence: '0', heartbeat_seconds: 10, capabilities: ['command']};
    }
    if (request.sequence) seen.set(request.sequence, reply);
    if (loseReply) { loseReply = false; throw new Error('lost response'); }
    return reply;
  },
  async frame({frame_id}) { return frames.get(frame_id); }
};

const displayed = [];
const client = new ViewerClient(transport, {onView: async (view, bytes) => displayed.push([view, bytes])});
await client.join('demo', vp);
assert.equal(client.view.frame.width, 600);
assert.deepEqual(client.resumeCredentials, {client_id: 'c', resume_token: 'token'});
loseReply = true;
await assert.rejects(client.action({type: 'command', text: 'right'}), /lost response/);
await assert.rejects(client.snapshot(), e => e.code === 'uncertain_request');
await client.retry();
assert.equal(applied, 1);
assert.deepEqual(requests.at(-1), requests.at(-2));
assert.equal(client.view.revision, '2');
await client.resize({width: 601, height: 301, pixel_ratio: 1.5});
assert.equal(client.view.frame.width, 902);
assert.equal(client.view.frame.height, 452);
await client.action({type: 'pointer', phase: 'press', x: 30, y: 20, button: 'left', modifiers: []});
assert.ok(requests.at(-1).frame_id);
assert.equal(requests.at(-1).expected_revision, '2');
await client.heartbeat();
await client.leave();
assert.equal(client.identity, null);
await assert.rejects(client.snapshot(), e => e.code === 'not_joined');
assert.equal(displayed.length, 4);
assert.throws(() => framebuffer({width: 8192, height: 8192, pixel_ratio: 2}));
console.log('Viewer browser adapter tests passed');
