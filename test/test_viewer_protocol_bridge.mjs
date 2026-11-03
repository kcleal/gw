// Test the actual compiled EM_JS bridge: source-only tests miss C escape handling.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {ViewerClient, httpTransport} from '../deps/protocol/viewer/v1/client.mjs';

const bundle = readFileSync(new URL('../deps/wasm_remote/out/viewer.js', import.meta.url), 'utf8');
const start = bundle.indexOf('remote.convert=');
const end = bundle.indexOf(';remote.load=', start);
assert.ok(start >= 0 && end > start, 'Compiled remote action bridge not found');
const context = vm.createContext({remote: {state: {pixel_ratio: 2}}});
vm.runInContext(bundle.slice(start, end), context);
const convert = action => JSON.parse(JSON.stringify(context.remote.convert(action)));

for (const phase of ['press', 'move', 'release', 'wheel', 'cancel']) {
  const a = convert({type: 'input', value: `${phase} 840 360 0 3 0 1`});
  assert.deepEqual(a, {type: 'pointer', phase, x: 420, y: 180, button: 'left',
    modifiers: ['shift', 'control'], dx: 0, dy: 1});
}
assert.deepEqual(convert({type: 'settings', value: 'snp_threshold 100000\nsoft_clip_threshold 0\n'}),
  {type: 'settings', values: {snp_threshold: 100000, soft_clip_threshold: 0}});
assert.deepEqual(convert({type: 'command', value: 'vscroll 0 12'}), {type: 'vscroll', collection_id: '0', offset: 12});
assert.deepEqual(convert({type: 'command', value: 'track-resize 1 500'}), {type: 'track_resize', track_id: '1', y: 250});
assert.deepEqual(convert({type: 'command', value: 'key 263 2', region_index: 0}),
  {type: 'key', key: 'ArrowLeft', modifiers: ['control'], region_index: 0});
console.log('PASS: compiled EM_JS pointer phases, DPI, settings, scrollbar, track resize and hotkey conversion');

if (process.argv[2]) {
  const base = process.argv[2];
  const client = new ViewerClient(httpTransport({messageUrl: `${base}/viewer/v1/message`, frameUrl: `${base}/viewer/v1/frame`}));
  try {
    await client.join('default', {width: 900, height: 600, pixel_ratio: 2});
    for (const phase of ['press', 'release'])
      await client.action(convert({type: 'input', value: `${phase} 900 132 0 0 0 0`}));
    assert.ok(client.view.client.popups.includes('seq '), 'Compiled pointer bridge did not open reference popup');
    const snp = client.view.shared.settings.snp_threshold;
    await client.action(convert({type: 'settings', value: `snp_threshold ${snp}\n`}));
    assert.equal(client.view.shared.settings.snp_threshold, snp);
    console.log('PASS: compiled bridge → live server reference click and settings update');
  } finally {
    if (client.identity) await client.leave();
  }
}
