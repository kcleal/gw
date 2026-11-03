#!/usr/bin/env python3
"""Fixture server: --track test/test.gff3 --track test/server_demo.bed."""
import json,shlex,sys,urllib.request,urllib.error
URL=sys.argv[1] if len(sys.argv)>1 else 'http://127.0.0.1:8766'
def request(path,body=None,status=200):
    req=urllib.request.Request(URL+path,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'})
    try: r=urllib.request.urlopen(req,timeout=60)
    except urllib.error.HTTPError as e: r=e
    with r:
        data=r.read(); assert r.status==status,(r.status,data[:500]); return data
def state(): return json.loads(request('/api/state'))
def action(kind,value='',**kw):
    return json.loads(request('/api/action',dict(type=kind,value=value,revision=state()['revision'],**kw)))
def tracks(s): return [(float(v[0]),float(v[1]),v[2]) for v in map(shlex.split,s['tracks'].splitlines())]
def frame(s): return request('/api/frame?revision='+str(s['revision']))
for ratio in [1,1.5,2]:
    action('resize',width=int(900*ratio),height=int(600*ratio),pixel_ratio=ratio)
    action('navigate','chr1:9900-10300')
    action('command','track-height 0 0.2'); action('command','track-height 1 0.2')
    before=state(); original=tracks(before); assert len(original)==2
    image=frame(before)
    after=action('command',f'track-resize 0 {original[0][0]-40*ratio}')
    grown=tracks(after)
    assert grown[0][0]<original[0][0] and sum(t[1] for t in grown)>sum(t[1] for t in original)
    assert before['regions']==after['regions'] and before['active_region']==after['active_region']
    assert image!=frame(after), 'Resizing did not update the image'
    after=action('command',f'track-resize 1 {grown[1][0]+10*ratio}')
    split=tracks(after)
    assert split[0][1]>grown[0][1] and split[1][1]<grown[1][1]
    assert abs(sum(t[1] for t in split)-sum(t[1] for t in grown))<1
    action('command','chr1:9950-10350')
    assert abs(tracks(state())[0][1]-split[0][1])<1, 'Track heights did not persist across navigation'
    action('navigate','chr1:9900-10300')
    s=state(); bed=tracks(s)[1]; gap=float(s['scrollbars'].splitlines()[0].split()[1])
    x=gap+(50/400)*(s['width']-2*gap); y=bed[0]+min(12*ratio,bed[1]*.5)
    action('input',f'press {x} {y} 0 0 0 0')
    s=action('input',f'release {x} {y} 0 0 0 0')
    assert 'track ' in s['popups'] and 'Demo_feature_A' in s['popups'], s['popups']
for value in ['track-resize -1 10','track-resize 99 10','track-resize 0 nan','track-resize 0 -1']:
    before=state(); request('/api/action',dict(type='command',value=value,revision=before['revision']),400)
    assert before==state()
print('PASS: track loading/popups, panel and adjacent separator resize, persistence, validation and DPI')
