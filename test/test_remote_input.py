#!/usr/bin/env python3
"""Run against gw-server using test/test.fa and test/test.bam."""
import json
import sys
import urllib.request
import urllib.error

URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8767'


def request(path, data=None, status=200):
    req = urllib.request.Request(URL + path, data=None if data is None else json.dumps(data).encode(),
                                 headers={'Content-Type': 'application/json'})
    try:
        response = urllib.request.urlopen(req, timeout=60)
    except urllib.error.HTTPError as exc:
        response = exc
    with response:
        payload = response.read()
        assert response.status == status, (response.status, payload[:500])
        return payload


def state():
    return json.loads(request('/api/state'))


def action(kind, value='', **kw):
    return json.loads(request('/api/action', dict(type=kind, value=value, revision=state()['revision'], **kw)))


def pointer(kind, x, y, dx=0, dy=0):
    return action('input', f'{kind} {x} {y} 0 0 {dx} {dy}')


def click(x, y):
    pointer('press', x, y)
    return pointer('release', x, y)


for ratio in [1, 2, 1.5]:
    action('resize', width=int(900*ratio), height=int(500*ratio), pixel_ratio=ratio)
    action('navigate', 'chr1:9900-10300')
    action('command', 'theme dark')
    s = state()
    request('/api/frame?revision=' + str(s['revision']))
    # Reference, coverage and read hit tests use original GW popup models.
    s = click(450*ratio, 66*ratio)
    assert 'seq ' in s['popups'], (ratio, s['popups'])
    s = click(450*ratio, 100*ratio)
    assert 'cov ' in s['popups'], (ratio, s['popups'])
    found = None
    for y in range(155, 320, 8):
        s = click(450*ratio, y*ratio)
        if 'read ' in s['popups']:
            found = s['popups']
            break
    assert found, 'No read selected'
    uid = int(found.split()[1])
    action('command', f'read-popup {uid} find')
    before = state()['region']
    pointer('press', 450*ratio, 300*ratio)
    pointer('move', 470*ratio, 300*ratio)
    s = pointer('release', 470*ratio, 300*ratio)
    assert s['region'] != before, 'Drag did not pan'
    assert not s['popups'], 'Dragging opened a click popup'
    pointer('press', 450*ratio, 300*ratio)
    pointer('cancel', 450*ratio, 300*ratio)
    before = state()['region']
    pointer('move', 480*ratio, 300*ratio)
    assert state()['region'] == before, 'Cancelled drag remained active'
    s = pointer('wheel', 450*ratio, 300*ratio, dy=1)
    assert s['region'] != before, 'Wheel did not zoom'

# Preserve a live popup even when subsequent input is rejected.
action('navigate', 'chr1:9900-10300')
click(450*ratio, 100*ratio)
assert state()['popups']
# Rejected pointer events must not change revision, view or popups.
for value in ['press nan 100 0 0 0 0', 'press -1 0 0 0 0 0', 'release 0 0 9 0 0 0',
              'press 0 0 1 0 0 0', 'bogus 0 0 0 0 0 0', 'wheel 0 0 0 0 0 999', 'press 0 0 0 0 0 0 garbage']:
    before = state()
    request('/api/action', dict(type='input', value=value, revision=before['revision']), 400)
    assert state() == before
s = state()
request('/api/action', dict(type='input', value='press 0 0 0 0 0 0', revision=s['revision']-1), 409)
print('PASS: reference/coverage/read popups, read action, drag, cancel, wheel, DPI and invalid/stale events')

# Tools uses multiple consecutive commands for filters; each is valid server-side.
action('navigate', 'chr1:9900-10300')
for command in ['refresh', 'filter mapq >= 30', 'filter ~flag & 1024', 'find missing_read',
                'refresh', 'sort strand 10100', 'sort none', 'introns', 'introns',
                'mismatches', 'mismatches', 'log2-cov', 'log2-cov']:
    result = action('command', command)
    request('/api/frame?revision=' + str(result['revision']))
result = action('command', 'help')
assert result['output'], 'Native command output was discarded'
result = action('command', 'refresh')
assert not result['output'], 'Command output replayed on next action'
print('PASS: Tools filters, find, sort position, introns, display aliases and command output')

# Remote scrollbars expose layout and apply the same read-stack offset as native GW.
s = action('navigate', 'chr1:9900-10300')
assert len(s['scrollbars'].splitlines()) >= 2
before = request('/api/frame?revision=' + str(s['revision']))
s = action('command', 'vscroll 0 10')
assert int(s['scrollbars'].splitlines()[1].split()[4]) == 10
after = request('/api/frame?revision=' + str(s['revision']))
assert before != after, 'Scrollbar offset did not change rendering'
action('command', 'vscroll 0 0')
for command in ['mm', 'ins', 'edges', 'soft-clips']:
    saved = state()['settings']
    s = action('command', command)
    s = action('command', command)
    assert s['settings'] == saved, f'{command} did not restore its enabled threshold'
print('PASS: scrollbar layout/offset/render and repeated threshold toggles')

# Native scale-bar selection: both drag directions, cancellation, and HiDPI.
for ratio in [1, 1.5, 2]:
    action('resize', width=int(900*ratio), height=int(500*ratio), pixel_ratio=ratio)
    action('settings', 'font_size 12\nscale_bar 1\n')
    selected = []
    for left, right in [(225,675), (675,225)]:
        action('navigate','chr1:9900-10300')
        pointer('press', left*ratio, 42*ratio)
        # Client previews locally, so no movement request is necessary.
        result = pointer('release', right*ratio, 42*ratio)
        start,end = map(int,result['region'].split(':')[1].split('-'))
        assert 9900 < start < end < 10300, result['region']
        assert 190 <= end-start <= 220, result['region']
        selected.append(result['region'])
    assert selected[0] == selected[1]
    action('navigate','chr1:9900-10300')
    pointer('press',225*ratio,42*ratio)
    pointer('cancel',675*ratio,42*ratio)
    assert state()['region'] == 'chr1:9900-10300'
    pointer('press',225*ratio,42*ratio)
    pointer('release',226*ratio,42*ratio)
    assert state()['region'] == 'chr1:9900-10300'

# Commands are resolved by GW, including shorthand and native error reporting.
assert action('command','chr1')['region'].startswith('chr1:')
assert action('command','chr1:10,000')['region'] == 'chr1:10000-10001'
assert action('command','goto chr1:9900-10300')['region'] == 'chr1:9900-10300'
for command in ['unknown_command', 'sort bogus', 'theme unknown']:
    before=state()['region']
    result=action('command',command)
    assert result['region'] == before and result['output'], (command,result)
print('PASS: scale-bar box zoom, reverse/cancel/tiny drags, DPI, shorthand and native command errors')

# Multiple region panes, active-pane navigation, native hotkeys, and resize.
action('navigate','chr1:9900-10300')
s=action('command','add chr1:9900-10300')
assert len(s['regions'])==2
s=action('command','chr1:10000-10400',region_index=1)
assert s['regions']==['chr1:9900-10300','chr1:10000-10400'] and s['active_region']==1
s=action('command','key 263 0',region_index=1) # Left
assert s['regions'][0]=='chr1:9900-10300' and s['regions'][1]!='chr1:10000-10400'
s=action('command','key 91 0') # Previous region
assert s['active_region']==0
before=s['regions'][0]
s=action('command','key 265 0') # Up/zoom
assert s['regions'][0]!=before
s=action('command','key 76 0') # Cycle links
assert 'link_op 1' in s['settings']
action('command','link none')
for width,height,dpr in [(800,600,1),(1200,800,2),(900,450,1.5),(500,700,1)]:
    s=action('resize',width=width,height=height,pixel_ratio=dpr)
    assert len(s['regions'])==2
    image=request('/api/frame?revision='+str(s['revision']))
    assert __import__('struct').unpack('>II',image[16:24])==(width,height)
s=action('command','rm 1')
assert len(s['regions'])==1
s=action('command','rm 0')
assert s['regions']==[] and s['region']==''
request('/api/frame?revision='+str(s['revision']))
s=action('command','chr1:9900-10300',region_index=0)
assert s['regions']==['chr1:9900-10300']
print('PASS: add/close/last-close/reopen, active pane, navigation hotkeys and multi-region resizing')
