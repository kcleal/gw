/**
 * Mount the native GW WASM UI inside a host-owned element.
 *
 * params: a startup view, as the page's URL parameters (see shell.html), e.g.
 *   {genome: 'hg38', bam: [urlA, urlB], name: ['Daughter', 'Father'], region: 'chr11:5227000-5227030'}
 * focus: take the keyboard when GW starts. Off by default, so a host such as a slide deck
 *   keeps its keys until GW is clicked.
 */
export function createViewer(container, {url = './gw.html', params = {}, focus = false, onState, onError} = {}) {
    const target = new URL(url, window.location.href);
    if (target.pathname.endsWith('/')) target.pathname += 'gw.html';
    target.searchParams.set('embed', '1');
    if (focus) target.searchParams.set('focus', '1');
    for (const [key, value] of Object.entries(params))
        for (const item of [].concat(value)) if (item !== undefined && item !== null) target.searchParams.append(key, String(item));

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
    const command = text => call('action', {type:'command', text});
    const snapshot = () => call('snapshot');
    // Opens a file and, given a name, labels it. Resolves once the file is open.
    async function open(prefix, kind, url, name) {
        const before = ((await snapshot())?.shared?.[kind] || []).length;
        await command(prefix + url);
        if (name) await command(`name ${kind === 'tracks' ? 'track' : 'bam'}${before} ${name}`);
        return before;
    }
    return {
        element: frame, ready,
        navigate: region => call('action', {type:'navigate', region}),
        command, snapshot,
        /** Open a BAM/CRAM (URL or path); resolves to its bam index. */
        load: (url, {name} = {}) => open('load ', 'alignments', url, name),
        /** Open an annotation track (bigBed, bigWig, BED/GFF/VCF); resolves to its track index. */
        loadTrack: (url, {name} = {}) => open('load track ', 'tracks', url, name),
        /**
         * Open a VCF/BED as a grid of image tiles, one per variant, and show the grid.
         * grid: tiles across x down, e.g. '4x3' (GW's default is 1x1, one variant per screen).
         * command('tiles off') or navigate() returns to the normal view, 'tiles on' back.
         */
        loadTiled: async (url, {grid} = {}) => {
            if (grid) await command('grid ' + grid);
            return open('load tiled ', 'tiled', url);
        },
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

