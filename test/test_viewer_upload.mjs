import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {ViewerClient,httpTransport} from '../deps/protocol/viewer/v1/client.mjs';
const url=process.argv[2] || 'http://127.0.0.1:8767/';
globalThis.window={location:{href:url}};
const served=await fetch(new URL('embed.mjs',url));assert.equal(served.status,200);
const {uploadDataset}=await import('data:text/javascript;base64,'+Buffer.from(await served.text()).toString('base64'));
const bam=new File([await readFile(new URL('./test.bam',import.meta.url))],'test.bam');
const index=new File([await readFile(new URL('./test.bam.bai',import.meta.url))],'test.bam.bai');
await assert.rejects(uploadDataset([index],{url}),/one BAM/);
let uploaded,client;
const progress=[];
try {
 uploaded=await uploadDataset([bam,index],{url,onProgress:p=>progress.push(p)});
 assert.equal(progress.at(-1),1);
 const base=new URL(uploaded.transportBase,url).href;
 client=new ViewerClient(httpTransport({messageUrl:base+'/message',frameUrl:base+'/frame'}));
 await client.join('default',{width:700,height:400,pixel_ratio:1});
 assert.equal(client.view.shared.alignments.length,1);
 assert.equal(client.view.frame.width,700);
 await client.action({type:'navigate',region:'chr1:9900-10300'});
 assert.ok(client.view.shared.regions[0].includes('chr1'));
 const peer=new ViewerClient(httpTransport({messageUrl:base+'/message',frameUrl:base+'/frame'}));
 await peer.join('default',{width:600,height:300,pixel_ratio:1});
 await client.action({type:'command',text:'rm bam0'});
 assert.equal(client.view.shared.alignments.length,0);
 await peer.snapshot();assert.equal(peer.view.shared.alignments.length,0);await peer.leave();
 const newcomer=new ViewerClient(httpTransport({messageUrl:base+'/message',frameUrl:base+'/frame'}));
 await newcomer.join('default',{width:600,height:300,pixel_ratio:1});
 assert.equal(newcomer.view.shared.alignments.length,0);await newcomer.leave();
 await client.leave();client=null;
 await uploaded.dispose();
 const r=await fetch(base+'/open',{method:'POST'});assert.equal(r.status,400);
 uploaded=null;
 await assert.rejects(uploadDataset([bam],{url}),/Waiting for index/);
 const reloaded=await uploadDataset([bam,index],{url});await reloaded.dispose();
 // The server must refuse a missing index even if a caller bypasses the picker.
 const bare=new URL('uploads/'+crypto.randomUUID(),url);
 try {
  const sent=await fetch(bare.href+'/files/bam?offset=0',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:bam});assert.equal(sent.status,200);
  const refused=await fetch(bare.href+'/open',{method:'POST'});assert.equal(refused.status,400);assert.match(await refused.text(),/Waiting for index/);
 } finally {await fetch(bare,{method:'DELETE'});}

 const badIndex=new File([new Uint8Array([1,2,3])],'test.bam.bai');
 await assert.rejects(uploadDataset([bam,badIndex],{url}),/valid index|readable|index/);
 console.log('PASS: chunked BAM/index upload, isolated protocol view, navigation, deletion and invalid index rejection');
} finally {
 if(client)await client.leave().catch(()=>{});
 if(uploaded)await uploaded.dispose().catch(()=>{});
}
