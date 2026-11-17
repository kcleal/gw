import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
import {mountLocalFile} from '../deps/wasm/local-files.mjs';
const path=process.argv[2];
if(!path)throw new Error('Supply the compiled Emscripten fixture .mjs path');
const {default:initialize}=await import(pathToFileURL(path));
const wasmBinary=await readFile(path.replace(/\.mjs$/,'.wasm'));
const Module=await initialize({wasmBinary});
const source=Uint8Array.from({length:100000},(_,i)=>i%251),reads=[];
const file={size:source.length,slice(a,b){reads.push([a,b]);return {async arrayBuffer(){await new Promise(r=>setTimeout(r,1));return source.slice(a,b).buffer;}}},arrayBuffer(){throw new Error('Whole-file read forbidden');}};
const mounted=mountLocalFile(Module.FS,Module,'/local','fixture.bam',file);
for(const method of ['range_stdio','range_pread'])for(const offset of [7,4097,99100]){
 const actual=await Module.ccall(method,'number',['string','number'],[mounted,offset],{async:true});
 assert.equal(actual,source.slice(offset,offset+5).reduce((a,b)=>a+b,0),method+' must survive suspension/rewind');
}
assert.ok(reads.length>=6);assert.ok(reads.every(([a,b])=>b-a<source.length));
console.log('PASS: actual WASM libc fread/fseek/pread with async File slices and correct byte results');
if(Module._range_bam){
 const bam=await readFile(new URL('./test.bam',import.meta.url));
 const bai=await readFile(new URL('./test.bam.bai',import.meta.url));
 Module.FS.writeFile('/baseline.bam',bam);Module.FS.writeFile('/baseline.bam.bai',bai);
 function local(name,bytes){return mountLocalFile(Module.FS,Module,'/genomics',name,{size:bytes.length,slice(a,b){return {async arrayBuffer(){await new Promise(r=>setTimeout(r,1));return Uint8Array.from(bytes.subarray(a,b)).buffer;}}}});}
 local('reads.bam.bai',bai);const lazy=local('reads.bam',bam);
 const baseline=await Module.ccall('range_bam','number',['string'],['/baseline.bam'],{async:true});
 const actual=await Module.ccall('range_bam','number',['string'],[lazy],{async:true});
 assert.ok(baseline>0);assert.equal(actual,baseline);
 console.log('PASS: real HTSlib indexed BAM region query returns identical reads through async File slices');
}
