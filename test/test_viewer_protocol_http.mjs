// Exercise the shipped browser adapter against a real oat++ server without a DOM.
import assert from 'node:assert/strict';
import {ViewerClient, httpTransport} from '../deps/protocol/viewer/v1/client.mjs';
const base = process.argv[2] || 'http://127.0.0.1:8766';
const transport = httpTransport({messageUrl: `${base}/viewer/v1/message`, frameUrl: `${base}/viewer/v1/frame`});
let updates = 0, lost = false;
const a = new ViewerClient({frame: transport.frame, async exchange(message) {
  const reply = await transport.exchange(message);
  if (lost) { lost = false; throw new Error('simulated lost HTTP reply'); }
  return reply;
}}, {onView: () => {updates++;}});
const b = new ViewerClient(transport);
try {
  await a.join('default', {width: 900, height: 600, pixel_ratio: 1});
  await b.join('default', {width: 601, height: 401, pixel_ratio: 1.5});
  assert.equal(a.view.frame.width, 900); assert.equal(b.view.frame.width, 902);
  await a.action({type: 'command', text: 'chr1:9900-10300'});
  await b.snapshot();
  assert.deepEqual(a.view.shared.regions, b.view.shared.regions);
  lost = true;
  await assert.rejects(a.action({type: 'command', text: 'theme slate'}), /simulated/);
  await a.retry();
  assert.equal(a.view.shared.theme, 'slate');
  await b.snapshot(); assert.equal(b.view.shared.theme, 'slate');
  await a.action({type: 'settings', values: {scale_bar: true, snp_threshold: 100000}});
  await a.action({type: 'pointer', phase: 'press', x: 450, y: 66, button: 'left', modifiers: []});
  await a.action({type: 'pointer', phase: 'release', x: 450, y: 66, button: 'left', modifiers: []});
  assert.ok(a.view.client.popups.includes('seq '));
  await b.snapshot(); assert.equal(b.view.client.popups, '');
  const id = a.identity.client_id, credentials = a.resumeCredentials;
  await a.join('default', {width: 800, height: 500, pixel_ratio: 2}, credentials);
  assert.equal(a.identity.client_id, id); assert.equal(a.view.frame.width, 1600);
  assert.equal(b.view.frame.width, 902);
  await a.heartbeat(); await b.heartbeat();
  assert.ok(updates >= 6);
  console.log('PASS: shipped JS HTTP adapter ↔ real oat++ server, PNGs, settings, private popups, retry, sharing and resume');
} finally {
  if (a.identity) await a.leave();
  if (b.identity) await b.leave();
}
