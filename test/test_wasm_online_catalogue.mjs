import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
const path=process.argv[2];
if(!path)throw Error('Supply the compiled test_wasm_online_catalogue fixture .mjs');
const {default:initialize}=await import(pathToFileURL(path));
const Module=await initialize({wasmBinary:await readFile(path.replace(/\.mjs$/,'.wasm'))});
const bundled=await readFile(new URL('../include/resources/online/catalogue.json',import.meta.url),'utf8');
assert.equal(Module.ccall('catalogue_count','number',['string'],[bundled]),7);
for(const text of ['{broken','{"version":2,"entries":[]}','{"version":1,"entries":{}}'])
    assert.equal(Module.ccall('catalogue_count','number',['string'],[text]),-1);
assert.equal(Module.ccall('bundle_count','number',[],[]),4);
console.log('Actual WASM catalogue decoding, bundle resolution and recoverable JSON errors passed');
