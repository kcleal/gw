import {mountLocalFile} from './local-files.mjs';
const PAGE=256*1024, PAGES=32;
function httpURL(url){const u=new URL(url);if(!['https:','http:'].includes(u.protocol))throw Error('Use an HTTP(S) URL');return u.href;}
async function request(url,start,end,signal,knownSize){
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),30000);
    const abort=()=>controller.abort();signal?.addEventListener('abort',abort,{once:true});
    if(signal?.aborted)controller.abort();
    try{
        const response=await fetch(url,{headers:{Range:`bytes=${start}-${end}`},signal:controller.signal,credentials:'omit'});
        if(response.status!==206){await response.body?.cancel();throw Error(`HTTP ${response.status}: server must support byte ranges (206); whole-file downloads are disabled`);}
        const range=response.headers.get('Content-Range');
        const match=/^bytes (\d+)-(\d+)\/(\d+)$/.exec(range||'');
        let total,length;
        if(match){
            if(Number(match[1])!==start || Number(match[2])>end || Number(match[2])<start || !Number.isSafeInteger(Number(match[3]))){await response.body?.cancel();throw Error('Invalid Content-Range');}
            length=Number(match[2])-start+1;total=Number(match[3]);
            if(Number(match[2])!==Math.min(end,total-1)){await response.body?.cancel();throw Error('Incomplete HTTP byte range');}
        }else if(!range){
            // UCSC permits Range but does not expose Content-Range through CORS.
            // HEAD Content-Length is safelisted; require it and an exact partial length.
            total=knownSize;
            if(total===undefined){
                const head=await fetch(url,{method:'HEAD',signal:controller.signal,credentials:'omit'});
                total=head.ok?Number(head.headers.get('Content-Length')):0;
            }
            length=Math.min(end,total-1)-start+1;
            if(!Number.isSafeInteger(total)||total<=0||length<=0||Number(response.headers.get('Content-Length'))!==length){await response.body?.cancel();throw Error('Host must expose Content-Range, or provide HEAD and partial Content-Length headers');}
        }else{await response.body?.cancel();throw Error('Invalid Content-Range');}
        // Read at most the promised range, even if a broken host sends a whole file.
        const reader=response.body.getReader(),parts=[];let received=0;
        for(;;){const {done,value}=await reader.read();if(done)break;received+=value.length;if(received>length){await reader.cancel();throw Error('Server returned too much data for a byte range');}parts.push(value);}
        if(received!==length)throw Error('Truncated HTTP byte range');
        const data=new Uint8Array(length);let p=0;for(const part of parts){data.set(part,p);p+=part.length;}
        return {data,size:total,etag:response.headers.get('ETag')};
    }catch(error){if(controller.signal.aborted)throw Error(signal?.aborted?'Cancelled':'HTTP range request timed out');if(error instanceof TypeError)throw Error('Cannot fetch remote file: check its URL and browser CORS permissions');throw error;}
    finally{clearTimeout(timer);signal?.removeEventListener('abort',abort);}
}
export async function remoteFile(url,{signal,status=()=>{},pageSize=PAGE,cachePages=PAGES}={}){
    url=httpURL(url);const initial=await request(url,0,0,signal),cache=new Map();
    const file={name:decodeURIComponent(new URL(url).pathname.split('/').pop()),size:initial.size,
        slice(start,end){return {async arrayBuffer(){
            start=Math.max(0,start);end=Math.min(file.size,end);
            const result=new Uint8Array(Math.max(0,end-start));
            for(let pos=start;pos<end;){
                const page=Math.floor(pos/pageSize),begin=page*pageSize;let bytes=cache.get(page);
                if(!bytes){try{const next=await request(url,begin,Math.min(begin+pageSize,file.size)-1,signal,file.size);if(next.size!==file.size || (initial.etag&&next.etag&&initial.etag!==next.etag))throw Error('Remote file changed; close and reopen it');bytes=next.data;cache.set(page,bytes);if(cache.size>cachePages)cache.delete(cache.keys().next().value);}catch(error){status(error.message);throw error;}}
                else{cache.delete(page);cache.set(page,bytes);}
                const n=Math.min(end-pos,bytes.length-(pos-begin));if(n<=0)throw Error('Invalid cached range');result.set(bytes.subarray(pos-begin,pos-begin+n),pos-start);pos+=n;
            }
            return result.buffer;
        }};}
    };return file;
}
export async function fetchCatalogue(url,{signal}={}){
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),30000);
    const abort=()=>controller.abort();signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)controller.abort();
    try{
        const response=await fetch(httpURL(url),{signal:controller.signal,credentials:'omit'});if(!response.ok)throw Error(`Catalogue HTTP ${response.status}`);
        const reader=response.body.getReader(),parts=[];let size=0;
        for(;;){const {done,value}=await reader.read();if(done)break;size+=value.length;if(size>2*1024*1024){await reader.cancel();throw Error('Catalogue exceeds 2 MiB');}parts.push(value);}
        return await new Blob(parts).text();
    }catch(error){if(controller.signal.aborted)throw Error(signal?.aborted?'Cancelled':'Catalogue request timed out');throw error;}
    finally{clearTimeout(timer);signal?.removeEventListener('abort',abort);}
}
function indexCandidates(entry){if(entry.indexURL)return [entry.indexURL];const u=new URL(entry.url),path=u.pathname;
    const make=s=>{const v=new URL(u);v.pathname=s;return v.href;};
    if(entry.category==='reference')return [make(path+'.fai')];
    if(path.endsWith('.bam'))return [make(path+'.bai'),make(path.slice(0,-4)+'.bai'),make(path+'.csi')];
    if(path.endsWith('.cram'))return [make(path+'.crai'),make(path.slice(0,-5)+'.crai')];
    if(path.endsWith('.bcf'))return [make(path+'.csi')];
    if(/\.(gz|bgz)$/.test(path))return [make(path+'.tbi'),make(path+'.csi')];return [];
}
// Indexes are small compared with genomic data, but HTSlib scans them in full.
// Prepare once outside Asyncify so validation/reopening do not repeat serial HTTP reads.
const MAX_INDEX=64*1024*1024, INDEX_BLOCK=1024*1024;
async function readPreparedFile(file,options,label){
    const bytes=new Uint8Array(file.size);let next=0,completed=0;
    async function worker(){
        for(;;){const start=next;next+=INDEX_BLOCK;if(start>=file.size)return;
            const part=new Uint8Array(await file.slice(start,Math.min(start+INDEX_BLOCK,file.size)).arrayBuffer());
            bytes.set(part,start);completed+=part.length;
            options.status?.(`Loading ${label}: ${(completed/1048576).toFixed(1)} / ${(file.size/1048576).toFixed(1)} MiB…`);
        }
    }
    await Promise.all(Array.from({length:4},worker));return bytes;
}
async function prepareIndex(url,options){
    const controller=new AbortController(),abort=()=>controller.abort();
    options.signal?.addEventListener('abort',abort,{once:true});
    if(options.signal?.aborted)controller.abort();
    try{
        const file=await remoteFile(url,{...options,signal:controller.signal,pageSize:INDEX_BLOCK,cachePages:0});
        if(file.size>MAX_INDEX)throw Error('Index exceeds 64 MiB; use a smaller regional dataset or a server-backed viewer');
        return await readPreparedFile(file,options,'index');
    }catch(error){controller.abort();throw error;}
    finally{options.signal?.removeEventListener('abort',abort);}
}
let serial=0;
export async function prepareOnline(entry,{FS,Module,signal,status=()=>{}}){
    status('Checking HTTP range access…');const data=await remoteFile(entry.url,{signal,status});
    const name=data.name.replace(/[^a-zA-Z0-9_.-]/g,'_');if(!name)throw Error('URL needs a filename with a supported extension');
    const directory='/online/'+(++serial),mounted=[];const cleanup=()=>{for(const path of mounted)try{FS.unlink(path);}catch(ignore){}try{FS.rmdir(directory);}catch(ignore){}};
    try{
        let index,indexURL;const candidates=indexCandidates(entry);
        if(candidates.length){status('Checking index…');let last;for(const url of candidates){try{index=await prepareIndex(url,{signal,status});indexURL=url;break;}catch(error){if(signal?.aborted)throw error;last=error;}}
            if(!index)throw Error('Cannot read required index: '+last.message);
        }
        const textFile=/\.(bed|vcf|gff|gff3|gtf)$/.test(name);
        if(textFile&&data.size>16*1024*1024)throw Error('Unindexed text exceeds 16 MiB; use an indexed BGZF or bigBed/bigWig file');
        const result={...entry,url:directory+'/'+name,indexURL:'',compressedIndexURL:''};
        if(textFile){
            const bytes=await readPreparedFile(data,{status},'annotation');
            FS.mkdirTree(directory);FS.createDataFile(directory,name,bytes,true,false,false);
        }else result.url=mountLocalFile(FS,Module,directory,name,data);
        mounted.push(result.url);
        if(index){const suffix=entry.category==='reference'?'.fai':new URL(indexURL).pathname.endsWith('.csi')?'.csi':new URL(indexURL).pathname.endsWith('.crai')?'.crai':new URL(indexURL).pathname.endsWith('.bai')?'.bai':'.tbi';FS.createDataFile(directory,name+suffix,index,true,false,false);result.indexURL=directory+'/'+name+suffix;mounted.push(result.indexURL);}
        if(entry.category==='reference' && /\.(gz|bgz)$/.test(name)){
            const u=new URL(entry.url);u.pathname+='.gzi';const gzi=await prepareIndex(entry.compressedIndexURL||u.href,{signal,status});FS.createDataFile(directory,name+'.gzi',gzi,true,false,false);result.compressedIndexURL=directory+'/'+name+'.gzi';mounted.push(result.compressedIndexURL);
        }
        return {entry:result,cleanup};
    }catch(error){cleanup();throw error;}
}

export async function fileSize(url){
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),10000);
    try{
        const response=await fetch(httpURL(url),{method:'HEAD',signal:controller.signal,credentials:'omit'});
        const length=response.headers.get('Content-Length');
        const size=length===null?-1:Number(length);
        return response.ok&&Number.isSafeInteger(size)&&size>=0?size:-1;
    }catch(ignore){return -1;}finally{clearTimeout(timer);}
}

/** Explicit full download, separate from the bounded regional streaming path. */
export async function downloadOnline(entry,{FS,directory,signal,progress=()=>{},maxBytes=512*1024*1024}){
    const url=httpURL(entry.url),name=new URL(url).pathname.split('/').pop().replace(/[^a-zA-Z0-9_.-]/g,'_');
    if(!name||name==='.'||name==='..')throw Error('The URL needs a filename');
    let filename=(entry.format==='ucsc-table'?entry.assembly+'.'+entry.providerId+'.bed':entry.format==='ucsc-raw'?entry.assembly+'.'+entry.providerId+'.tsv.gz':name).replace(/[^a-zA-Z0-9_.-]/g,'_');
    FS.mkdirTree(directory);
    const exists=path=>{try{FS.lookupPath(path);return true;}catch(ignore){return false;}};
    const dot=filename.indexOf('.'),stem=dot<0?filename:filename.slice(0,dot),extension=dot<0?'':filename.slice(dot);
    let path=directory+'/'+filename;
    for(let n=2;exists(path)||exists(path+'.part');++n)path=directory+'/'+stem+'_'+n+extension;
    async function save(source,target){
        const temporary=target+'.part';let stream;
        try{
            if(signal?.aborted)throw Error('Download cancelled');
            progress(0,0);
            const response=await fetch(httpURL(source),{signal,credentials:'omit'});
            if(!response.ok)throw Error('Download HTTP '+response.status);
            if(!response.body)throw Error('This browser cannot stream downloads');
            const total=Number(response.headers.get('Content-Length'))||0;
            if(total>maxBytes){await response.body.cancel();throw Error('This file exceeds the browser download limit (512 MiB). Use Open for regional streaming, or download with native GW.');}
            stream=FS.open(temporary,'w');let received=0;
            const reader=response.body.getReader();
            try{
                for(;;){const {done,value}=await reader.read();if(done)break;
                    if(signal?.aborted)throw Error('Download cancelled');
                    if(received+value.length>maxBytes)throw Error('Browser download limit exceeded. Use Open for regional streaming, or download with native GW.');
                    const count=FS.write(stream,value,0,value.length,received);
                    if(count!==value.length)throw Error('Could not save complete download');
                    received+=count;progress(received,total);
                }
            }finally{await reader.cancel().catch(()=>{});}
            if(total&&received!==total)throw Error('Incomplete download');
            FS.close(stream);stream=null;FS.rename(temporary,target);
        }catch(error){if(stream)FS.close(stream);try{FS.unlink(temporary);}catch(ignore){}throw error;}
    }
    const result={...entry,url:path,indexURL:'',compressedIndexURL:''};
    const candidates=['ucsc-table','ucsc-raw'].includes(entry.format)?[]:indexCandidates(entry);
    if(candidates.length){let last;
        for(const candidate of candidates)try{
            const suffix=new URL(candidate).pathname.split('.').pop();
            const target=path+'.'+suffix;await save(candidate,target);result.indexURL=target;break;
        }catch(error){if(signal?.aborted)throw error;last=error;}
        if(!result.indexURL)throw Error('Data downloaded, but its required index failed: '+last?.message);
    }
    if(entry.category==='reference'&&/\.(gz|bgz)$/.test(new URL(url).pathname)){
        const u=new URL(url);u.pathname+='.gzi';result.compressedIndexURL=path+'.gzi';
        await save(entry.compressedIndexURL||u.href,result.compressedIndexURL);
    }
    if(entry.format==='ucsc-table'){
        const compressed=path+'.source.gz';
        try{
            await save(url,compressed);
            if(typeof DecompressionStream==='undefined')throw Error('This browser cannot decompress UCSC table downloads');
            const bytes=FS.readFile(compressed),reader=new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip')).pipeThrough(new TextDecoderStream()).getReader();
            let stream=FS.open(path+'.part','w'),pending='',position=0;
            try{
                for(;;){const {done,value}=await reader.read();if(done)break;
                    if(signal?.aborted)throw Error('Download cancelled');pending+=value;
                    let end;while((end=pending.indexOf('\n'))>=0){
                        const line=pending.slice(0,end);pending=pending.slice(end+1);
                        const bed=new TextEncoder().encode(tableRowToBed(line,entry.tableColumns));
                        if(position+bed.length>maxBytes)throw Error('Converted table exceeds the browser download limit');
                        if(FS.write(stream,bed,0,bed.length,position)!==bed.length)throw Error('Could not save converted track');position+=bed.length;
                    }
                    if(pending.length>16*1024*1024)throw Error('UCSC table row is too large');
                }
                if(pending){const bed=new TextEncoder().encode(tableRowToBed(pending,entry.tableColumns));if(position+bed.length>maxBytes)throw Error('Converted table exceeds browser download limit');FS.write(stream,bed,0,bed.length,position);}
                FS.close(stream);stream=null;FS.rename(path+'.part',path);
            }finally{if(stream)FS.close(stream);await reader.cancel().catch(()=>{});}
            result.format='bed';result.tableColumns='';
        }catch(error){try{FS.unlink(path+'.part');}catch(ignore){}throw error;}
        finally{try{FS.unlink(compressed);}catch(ignore){}}
    }else await save(url,path);
    return result;
}

export function tableRowToBed(row,columns){
    if(!row||row[0]==='#')return '';
    const values=row.replace(/\r$/,'').split('\t'),names=columns.split(',');
    if(values.length!==names.length)throw Error('UCSC table does not match its column schema');
    const fields=Object.fromEntries(names.map((name,i)=>[name,values[i]]));
    const number=value=>{if(!/^\d+$/.test(value))throw Error('Invalid UCSC table coordinate');const n=Number(value);if(!Number.isSafeInteger(n))throw Error('Invalid UCSC table coordinate');return n;};
    const start=number(fields.chromStart??fields.txStart),end=number(fields.chromEnd??fields.txEnd);
    if(!fields.chrom||end<start)throw Error('Invalid UCSC interval');
    const strand=['+','-'].includes(fields.strand)?fields.strand:'.';
    const score=/^\d+$/.test(fields.score||'')?Math.min(1000,Number(fields.score)):0;
    const bed=[fields.chrom,start,end,fields.name??fields.name2??'.',score,strand];
    const countText=fields.exonCount??fields.blockCount;
    if(countText!==undefined){
        const count=number(countText);if(count>1000000)throw Error('Too many UCSC exon blocks');
        const list=value=>(value||'').split(',').filter(value=>value!=='').map(number);
        const starts=list(fields.exonStarts??fields.chromStarts??fields.blockStarts),ends=list(fields.exonEnds),sizes=list(fields.blockSizes);
        if(starts.length!==count||(ends.length?ends.length:sizes.length)!==count)throw Error('Invalid UCSC exon blocks');
        const relative=starts.map(pos=>ends.length?pos-start:pos),lengths=starts.map((pos,i)=>ends.length?ends[i]-pos:sizes[i]);
        if(relative.some((pos,i)=>pos<0||lengths[i]<0||pos+lengths[i]>end-start))throw Error('Exon outside UCSC transcript');
        const thickStart=number(fields.thickStart??fields.cdsStart??String(start)),thickEnd=number(fields.thickEnd??fields.cdsEnd??String(end));
        if(thickStart<start||thickEnd<thickStart||thickEnd>end)throw Error('Invalid UCSC coding interval');
        bed.push(thickStart,thickEnd,0,count,lengths.join(',')+',',relative.join(',')+',');
    }
    return bed.join('\t')+'\n';
}
