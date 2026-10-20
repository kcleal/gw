/** Mount the native GW WASM UI inside a host-owned element. */
export function createViewer(container, {url = '/', session = 'default', transportBase, onState, onError, onFiles} = {}) {
    const target = new URL(url, window.location.href);
    // oat++'s root route does not match '/' when a query string is present.
    // Use the explicit shell asset for directory URLs, including subpath mounts.
    if (target.pathname.endsWith('/')) target.pathname += 'index.html';
    target.searchParams.set('embed', '1');
    target.searchParams.set('session', session);
    if (transportBase) target.searchParams.set('transport', transportBase);
    const channel = crypto.randomUUID();
    target.hash = channel;
    const frame = document.createElement('iframe');
    frame.title = 'GW genome viewer';
    frame.src = target.href;
    frame.style.cssText = 'display:block;border:0;width:100%;height:100%;min-height:100px';
    frame.allow = 'clipboard-write';
    let serial = 0, disposed = false, resolveReady, rejectReady, finishDispose;
    const pending = new Map();
    const startupTimer = setTimeout(() => {
        const error = new Error('GW did not finish loading; check the viewer URL and server connection');
        rejectReady(error); onError?.(error);
    }, 60000);
    const ready = new Promise((resolve, reject) => { resolveReady = resolve; rejectReady = reject; });
    // Consumers can choose whether to await readiness.
    ready.catch(() => {});
    function receive(event) {
        if (event.source !== frame.contentWindow || event.origin !== target.origin || event.data?.channel !== channel) return;
        const message = event.data;
        if (message.type === 'ready') { clearTimeout(startupTimer); resolveReady(); }
        if (message.type === 'disposed') finishDispose?.();
        if (message.type === 'state') onState?.(message.value);
        if (message.type === 'files') onFiles?.(message.files);
        if (message.type === 'error') {
            clearTimeout(startupTimer);
            const error = new Error(message.error);
            rejectReady(error);
            onError?.(error);
        }
        if (message.type === 'reply') {
            const request = pending.get(message.id);
            if (!request) return;
            pending.delete(message.id);
            message.error ? request.reject(new Error(message.error)) : request.resolve(message.value);
        }
    }
    window.addEventListener('message', receive);
    container.append(frame);
    async function call(method, value) {
        await ready;
        if (disposed) throw new Error('Viewer destroyed');
        return new Promise((resolve, reject) => {
            const id = ++serial;
            pending.set(id, {resolve, reject});
            frame.contentWindow.postMessage({channel, type:'call', id, method, value}, target.origin);
        });
    }
    return {
        element: frame, ready,
        navigate: region => call('action', {type:'navigate', region}),
        command: text => call('action', {type:'command', text}),
        settings: values => call('action', {type:'settings', values}),
        snapshot: () => call('snapshot'),
        focus: () => frame.focus(),
        async destroy() {
            if (disposed) return;
            disposed = true;
            clearTimeout(startupTimer);
            // Best effort; server leases also clean up interrupted clients.
            const left = new Promise(resolve => { finishDispose = resolve; });
            frame.contentWindow?.postMessage({channel, type:'dispose'}, target.origin);
            rejectReady(new Error('Viewer destroyed'));
            for (const request of pending.values()) request.reject(new Error('Viewer destroyed'));
            pending.clear();
            await Promise.race([left, new Promise(resolve => setTimeout(resolve, 500))]);
            window.removeEventListener('message', receive);
            frame.remove();
        }
    };
}

/** Demo upload extension; hosts may substitute their own dataset-loading service. */
export async function uploadDataset(files, {url='/', onProgress=()=>{}}={}) {
    files=Array.from(files);
    const bams=files.filter(file=>file.name.toLowerCase().endsWith('.bam'));
    if(bams.length!==1) throw new Error('Drop one BAM together with its matching .bai or .csi index');
    const bam=bams[0], stem=bam.name.slice(0,-4);
    const index=files.find(file=>[bam.name+'.bai',stem+'.bai',bam.name+'.csi',stem+'.csi'].includes(file.name));
    if(!index) throw new Error('Waiting for index: '+bam.name+'.bai or '+bam.name+'.csi');
    if(files.length!==(index?2:1)) throw new Error('Drop only one BAM, optionally with its matching index');
    if(files.some(file=>file.size>8*1024**3)) throw new Error('Maximum upload size is 8 GiB per file');
    const base=new URL(url,window.location.href);
    if(!base.pathname.endsWith('/')) base.pathname=base.pathname.slice(0,base.pathname.lastIndexOf('/')+1);
    const endpoint=new URL('uploads/'+crypto.randomUUID(),base);
    const total=bam.size+(index?.size || 0); let done=0;
    async function request(path,options) {
        const response=await fetch(endpoint.href+path,options);
        if(!response.ok) throw new Error(await response.text() || 'Upload failed');
    }
    try {
        for(const [file,kind] of [[bam,'bam'],...(index?[[index,index.name.endsWith('.csi')?'csi':'bai']]:[])]) {
            for(let offset=0;offset<file.size;offset+=4*1024**2) {
                const chunk=file.slice(offset,offset+4*1024**2);
                await request('/files/'+kind+'?offset='+offset,{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:chunk});
                done+=chunk.size;onProgress(total?done/total:1);
            }
        }
        await request('/open',{method:'POST'});
        return {transportBase:endpoint.pathname,dispose:()=>request('',{method:'DELETE'})};
    } catch(error) {
        await request('',{method:'DELETE'}).catch(()=>{});throw error;
    }
}

/** Pair sequential drops/selections before starting any upload. */
export function createFileLoader({onReady, onStatus}) {
    let bam, index, busy=false;
    return {
        get waiting() {return !!bam && !index;},
        cancel() {if(!busy){bam=index=undefined;onStatus('');}},
        async accept(files) {
            if(busy) {onStatus('A file is already opening');return;}
            const chosen=Array.from(files);
            const bams=chosen.filter(f=>f.name.toLowerCase().endsWith('.bam'));
            if(bams.length>1 || chosen.some(f=>! /\.(bam|bai|csi)$/i.test(f.name))) {
                onStatus('Select one BAM, optionally with its .bai or .csi index');return;
            }
            if(bams.length) {bam=bams[0];index=undefined;}
            if(!bam) {onStatus('Select a BAM first, together with its index or before it');return;}
            const stem=bam.name.slice(0,-4);
            const indexes=chosen.filter(f=>/\.(bai|csi)$/i.test(f.name));
            index=indexes.find(f=>[bam.name+'.bai',stem+'.bai',bam.name+'.csi',stem+'.csi'].includes(f.name)) || index;
            if(!index) {onStatus('Waiting for index — drop or select '+bam.name+'.bai or '+bam.name+'.csi');return;}
            busy=true;
            try {await onReady([bam,index]);bam=index=undefined;}
            catch(error) {index=undefined;onStatus(error.message+' — select the matching index to retry');}
            finally {busy=false;}
        }
    };
}
