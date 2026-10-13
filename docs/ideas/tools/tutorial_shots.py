#!/usr/bin/env python3
"""Tutorial screenshots: build small workspaces and capture the states the tutorial pages describe."""
import os, re, sys
sys.path.insert(0, '/tmp')
import shot

SRC = shot.SRC
PALETTE = ['0.258823544,0.807843208,0.890196145,1', '0.698039234,0.874509871,0.541176498,1',
           '1,0.6,0.2,1', '0.9,0.4,0.6,1', '0.6,0.5,0.9,1']


import ctypes
def topmost_ok(win, ax, ay, w, h):
    """True when the window under the pointer, at the middle of the VIAMD window, is the VIAMD window (or its frame)."""
    x11 = shot.x11
    D = ctypes.c_void_p(shot.dpy)
    root = x11.XDefaultRootWindow(D)
    shot.move(ax + w // 2, ay + h - 8)
    r, c = ctypes.c_ulong(), ctypes.c_ulong(); rx = ctypes.c_int(); ry = ctypes.c_int(); wx = ctypes.c_int(); wy = ctypes.c_int(); m = ctypes.c_uint()
    x11.XQueryPointer(D, ctypes.c_ulong(root), ctypes.byref(r), ctypes.byref(c), ctypes.byref(rx), ctypes.byref(ry), ctypes.byref(wx), ctypes.byref(wy), ctypes.byref(m))
    top = c.value
    # parent frame of our window
    rr, pp = ctypes.c_ulong(), ctypes.c_ulong(); ch = ctypes.POINTER(ctypes.c_ulong)(); n = ctypes.c_uint()
    x11.XQueryTree(D, ctypes.c_ulong(win), ctypes.byref(rr), ctypes.byref(pp), ctypes.byref(ch), ctypes.byref(n))
    return top in (win, pp.value)

def series_block(kind, subplot, path, color, plot='line', bins=128):
    return ('[%s]\nSubplot=%d\nSource=script\nVariant=values\nPath=script/%s\nColor=%s\nPlotType=%s\nMarker=0\nMarkerSize=1\n'
            'BarWidth=1\nNumBins=%d\nUseColormap=0\nColormap=5\nColormapAlpha=1\n\n') % (kind, subplot, path, color, plot, bins)

def rep_block(name, flt, typ, cmap, enabled=1, scale='1,1,1,1', base='1,1,1,1', dyn=0):
    return ('[Representation]\nName=%s\nFilter=%s\nEnabled=%d\nType=%d\nColorMapping=%d\nBaseColor=%s\nSaturation=1\nTintColor=1,0,0,1\nTintScale=0\n'
            'SecondaryStructureColorUnknown=0.5,0.5,0.5,1\nSecondaryStructureColorCoil=0.860000014,0.860000014,0.860000014,1\n'
            'SecondaryStructureColorHelix=0.119999997,0.860000014,0.119999997,1\nSecondaryStructureColorSheet=0.119999997,0.119999997,0.860000014,1\n'
            'BondColor=0\nBondSharpness=0.5\nBondBaseColor=1,1,1,1\nParam=%s\nDynamicEval=%d\n\n') % (name, flt, enabled, typ, cmap, base, scale, dyn)

def workspace(files, script, windows, tl, dist, reps=None, frame=0, tl_subplots=2, dist_subplots=2, base=None):
    """tl/dist: list of (subplot, ident); base: text of a workspace whose other sections are kept."""
    s = ''
    if files:
        s += '[Files]\n' + ''.join('%s=%s\n' % kv for kv in files) + 'CoarseGrained=0\n\n'
    s += '[Animation]\nFrame=%g\nFps=10\nTension=0\nInterpolation=2\n\n' % frame
    s += '[Script]\nText="""%s"""\n\n' % script
    s += '[Windows]\n' + '\n'.join('%s=%d' % (k, 1 if k in windows else 0) for k in shot.ALL) + '\n\n'
    s += '[Timeline]\nNumSubplots=%d\n\n' % tl_subplots
    for i, (sp, ident) in enumerate(tl):
        s += series_block('TimelineSeries', sp, ident, PALETTE[i % len(PALETTE)])
    s += '[Distributions]\nNumSubplots=%d\n\n' % dist_subplots
    for i, (sp, ident) in enumerate(dist):
        s += series_block('DistributionSeries', sp, ident, PALETTE[i % len(PALETTE)], 'area')
    for r in (reps or []):
        s += rep_block(*r) if isinstance(r, tuple) else r
    return s

def run(name, ws_text, layout, steps, bin_path, link_aspirin=False, settle=16, show=None, links=()):
    """Write the workspace into the work folder, then run a capture session."""
    shot.BIN = bin_path
    shot.prepare('showcase_gui.via', show or [], layout) if link_aspirin else None
    if not link_aspirin:
        import shutil
        shutil.rmtree(shot.WORK, ignore_errors=True); os.makedirs(shot.WORK)
        ini = ''.join('[Window][%s]\nPos=%d,%d\nSize=%d,%d\nCollapsed=0\n\n' % ((n,) + g) for n, g in layout.items())
        open(os.path.join(shot.WORK, 'imgui.ini'), 'w').write(ini)
        for src, dst in links: os.symlink(src, os.path.join(shot.WORK, dst))
    open(os.path.join(shot.WORK, name + '.via'), 'w', encoding='utf-8').write(ws_text)
    # session() calls prepare() again, which would overwrite the folder: run the capture loop here instead
    return capture(name + '.via', steps, settle)

def capture(workspace, steps, settle):
    import subprocess, time
    from PIL import ImageGrab
    env = {'HOME': os.environ['HOME'], 'DISPLAY': ':0', 'PATH': '/usr/bin:/bin',
           'XAUTHORITY': os.environ.get('XAUTHORITY', os.path.expanduser('~/.Xauthority')),
           'XDG_RUNTIME_DIR': '/run/user/%d' % os.getuid()}
    p = subprocess.Popen([shot.BIN, workspace], cwd=shot.WORK, env=env, stdout=open('/tmp/viamd_shot.log', 'w'), stderr=subprocess.STDOUT)
    saved = []
    try:
        win = shot.find_window(p.pid)
        if not win:
            print('no window'); return saved
        time.sleep(3); shot.raise_and_size(win); time.sleep(settle); shot.raise_and_size(win); time.sleep(1.5)
        ax, ay, w, h = shot.geom(win)
        for st in steps:
            k = st[0]
            if k == 'wait': time.sleep(st[1])
            elif k == 'click': shot.click(ax + st[1], ay + st[2])
            elif k == 'rclick': shot.click(ax + st[1], ay + st[2], 3)
            elif k == 'dbl': shot.dclick(ax + st[1], ay + st[2])
            elif k == 'move': shot.move(ax + st[1], ay + st[2])
            elif k == 'key': shot.key(st[1])
            elif k == 'away': shot.move(ax + w - 30, ay + h - 30)
            elif k == 'eval':
                x, y, ww, hh = shot.SE
                shot.click(ax + x + ww // 2, ay + y + hh - 40); shot.key('Return', shift=True); time.sleep(st[1])
            elif k == 'guard':
                ok = False
                for _ in range(4):
                    shot.raise_and_size(win); time.sleep(1.2)
                    if topmost_ok(win, ax, ay, w, h): ok = True; break
                if not ok:
                    print('ABORT: the VIAMD window is not on top'); return saved
            elif k == 'cap' and len(st) > 3 and st[3] == 'noguard':
                im = ImageGrab.grab(xdisplay=':0'); c = st[2]
                box = (ax, ay, ax + w, ay + h) if not c else (ax + c[0], ay + c[1], ax + c[2], ay + c[3])
                path = os.path.join(shot.OUT, st[1] + '.png'); im.crop(box).save(path); saved.append(path); print('saved', path)
            elif k == 'cap':
                ok = False
                for _ in range(4):
                    shot.raise_and_size(win); time.sleep(1.2)
                    if topmost_ok(win, ax, ay, w, h): ok = True; break
                if not ok:
                    print('ABORT: the VIAMD window is not on top, nothing captured'); return saved
                shot.move(ax + w - 30, ay + h - 30); time.sleep(0.8)
                im = ImageGrab.grab(xdisplay=':0'); c = st[2]
                box = (ax, ay, ax + w, ay + h) if not c else (ax + c[0], ay + c[1], ax + c[2], ay + c[3])
                path = os.path.join(shot.OUT, st[1] + '.png'); im.crop(box).save(path); saved.append(path); print('saved', path)
        return saved
    finally:
        p.terminate()
        try: p.wait(5)
        except Exception: p.kill()
