import assert from 'node:assert/strict';
import {createViewer} from '../deps/wasm_remote/embed.mjs';
const listeners = new Set(), sent = [], frames = [];
globalThis.window = {location:{href:'https://host.test/app'},
 addEventListener:(_,fn)=>listeners.add(fn),removeEventListener:(_,fn)=>listeners.delete(fn)};
globalThis.document = {createElement:()=>{
 const frame={style:{},contentWindow:{postMessage:(message,origin)=>sent.push({message,origin})},focus(){this.focused=true},remove(){this.removed=true}};
 frames.push(frame);return frame;
}};
const container={append(){}}, states=[];
const a=createViewer(container,{url:'https://gw.test/',onState:s=>states.push(s)});
const b=createViewer(container,{url:'https://gw.test/'});
assert.equal(new URL(a.element.src).pathname, '/index.html');
assert.equal(new URL(a.element.src).searchParams.get('session'), 'default');
function emit(viewer,type,extra={},origin='https://gw.test',source=viewer.element.contentWindow){
 const channel=new URL(viewer.element.src).hash.slice(1);
 for(const fn of listeners) fn({origin,source,data:{channel,type,...extra}});
}
let ready=false;a.ready.then(()=>ready=true);
emit(a,'ready',{},'https://evil.test');await Promise.resolve();assert.equal(ready,false);
emit(a,'ready',{},'https://gw.test',b.element.contentWindow);await Promise.resolve();assert.equal(ready,false);
emit(a,'ready');await a.ready;
const result=a.navigate('chr1:1-100');await Promise.resolve();
assert.deepEqual(sent[0].message.value,{type:'navigate',region:'chr1:1-100'});
assert.equal(sent[0].origin,'https://gw.test');
emit(a,'reply',{id:sent[0].message.id,value:'ok'});assert.equal(await result,'ok');
emit(a,'state',{value:{revision:'9'}});assert.deepEqual(states,[{revision:'9'}]);
const failed=a.command('bad');await Promise.resolve();emit(a,'reply',{id:sent[1].message.id,error:'busy'});
await assert.rejects(failed,/busy/);
a.focus();assert.ok(a.element.focused);
const destroyed=a.destroy();emit(a,'disposed');await destroyed;assert.ok(a.element.removed);
await assert.rejects(a.snapshot(),/destroyed/);
const waiting=b.navigate('chr2');const removed=b.destroy();emit(b,'disposed');await removed;
await assert.rejects(waiting,/destroyed/);assert.equal(listeners.size,0);
console.log('Embedded viewer lifecycle, origin isolation and host actions passed');

if (process.argv[2]) {
 const root = new URL(process.argv[2]);
 const moduleResponse = await fetch(new URL('embed.mjs', root));
 assert.equal(moduleResponse.status, 200);
 const servedSource = await moduleResponse.text();
 const {createViewer: servedCreateViewer} = await import('data:text/javascript;base64,' + Buffer.from(servedSource).toString('base64'));
 const live = servedCreateViewer(container, {url: root.href});
 const response = await fetch(live.element.src);
 assert.equal(response.status, 200, 'iframe URL must resolve with embed/session query parameters');
 assert.match(response.headers.get('content-type'), /text\/html/);
 const shell = await response.text();
 assert.match(shell, /viewer\.js/);
 assert.match(shell, /notify\('files'/);
 const leaving = live.destroy(); emit(live, 'disposed', {}, root.origin); await leaving;
 console.log('PASS: iframe startup URL resolves on the live server');
}

// A BAM alone must remain local until its matching index is selected later.
const {createFileLoader}=await import('../deps/wasm_remote/embed.mjs');
const messages=[],opened=[];
const loader=createFileLoader({onReady:async files=>opened.push(files),onStatus:text=>messages.push(text)});
const bam={name:'sample.bam'},idx={name:'sample.bam.bai'};
await loader.accept([bam]);assert.equal(loader.waiting,true);assert.equal(opened.length,0);
assert.match(messages.at(-1),/Waiting for index/);
await loader.accept([{name:'wrong.bam.bai'}]);assert.equal(opened.length,0);assert.equal(loader.waiting,true);
await loader.accept([idx]);assert.deepEqual(opened,[[bam,idx]]);assert.equal(loader.waiting,false);
await loader.accept([bam]);loader.cancel();assert.equal(loader.waiting,false);
console.log('PASS: sequential BAM/index selection, wrong index and cancellation');
