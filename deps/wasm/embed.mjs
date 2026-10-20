/** Mount the native GW WASM UI inside a host-owned element. */
export function createViewer(container, {url = './gw.html', onState, onError} = {}) {
    const target = new URL(url, window.location.href);
    if (target.pathname.endsWith('/')) target.pathname += 'gw.html';
    target.searchParams.set('embed', '1');

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
    return {
        element: frame, ready,
        navigate: region => call('action', {type:'navigate', region}),
        command: text => call('action', {type:'command', text}),
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

