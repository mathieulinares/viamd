#!/usr/bin/env python3
"""Screenshot helper for the wiki: launch VIAMD with a workspace and a layout, raise it, grab its window."""
import ctypes, ctypes.util, os, re, shutil, subprocess, sys, time
from PIL import ImageGrab

SRC = os.path.expanduser('~/Desktop/bioexcel/aspirin')
WORK = '/tmp/shotwork'
BIN = '/tmp/shotbin/viamd'
OUT = '/tmp/shots'
W, H, X0, Y0 = 1600, 900, 100, 60

x11 = ctypes.CDLL('libX11.so.6')
xtst = ctypes.CDLL('libXtst.so.6')
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
dpy = x11.XOpenDisplay(b':0')

def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout

def find_window(pid):
    # only the window of the process started here: never touch another VIAMD instance
    for _ in range(120):
        out = sh('xwininfo -root -tree')
        for line in out.splitlines():
            m = re.match(r'\s+(0x[0-9a-f]+) "VIAMD": \("VIAMD" "VIAMD"\)\s+(\d+)x(\d+)', line)
            if m and int(m.group(2)) > 400:
                wid = int(m.group(1), 16)
                owner = re.search(r'= (\d+)', sh('xprop -id %d _NET_WM_PID' % wid))
                if owner and int(owner.group(1)) == pid:
                    return wid
        time.sleep(0.5)
    return None

def geom(win):
    out = sh('xwininfo -id %d' % win)
    ax = int(re.search(r'Absolute upper-left X:\s+(-?\d+)', out).group(1))
    ay = int(re.search(r'Absolute upper-left Y:\s+(-?\d+)', out).group(1))
    w = int(re.search(r'Width:\s+(\d+)', out).group(1))
    h = int(re.search(r'Height:\s+(\d+)', out).group(1))
    return ax, ay, w, h

def raise_and_size(win):
    x11.XMoveResizeWindow(ctypes.c_void_p(dpy), ctypes.c_ulong(win), X0, Y0, W, H)
    x11.XRaiseWindow(ctypes.c_void_p(dpy), ctypes.c_ulong(win))
    x11.XSetInputFocus(ctypes.c_void_p(dpy), ctypes.c_ulong(win), 1, 0)
    x11.XFlush(ctypes.c_void_p(dpy))

def click(x, y, button=1):
    x11.XWarpPointer(ctypes.c_void_p(dpy), 0, x11.XDefaultRootWindow(ctypes.c_void_p(dpy)), 0, 0, 0, 0, x, y)
    x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.4)
    xtst.XTestFakeButtonEvent(ctypes.c_void_p(dpy), button, 1, 0); x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.08)
    xtst.XTestFakeButtonEvent(ctypes.c_void_p(dpy), button, 0, 0); x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.5)

def move(x, y):
    x11.XWarpPointer(ctypes.c_void_p(dpy), 0, x11.XDefaultRootWindow(ctypes.c_void_p(dpy)), 0, 0, 0, 0, x, y)
    x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.6)

ALL = ['Timelines', 'Distributions', 'Representations', 'ScriptEditor', 'Animation', 'Attributes',
       'Contacts', 'System', 'DensityVolume', 'Ramachandran', 'ShapeSpace']

def prepare(workspace, show, layout):
    shutil.rmtree(WORK, ignore_errors=True); os.makedirs(WORK)
    for f in os.listdir(SRC):
        if f.startswith('aspirin-phospholipase') or f.endswith('.pdb'):
            os.symlink(os.path.join(SRC, f), os.path.join(WORK, f))
    s = open(os.path.join(SRC, workspace), encoding='utf-8').read()
    def flag(m):
        return '%s=%d' % (m.group(1), 1 if m.group(1) in show else 0)
    sec = re.search(r'\[Windows\]\n(.*?)\n\n', s, re.S)
    new = '[Windows]\n' + '\n'.join('%s=%d' % (k, 1 if k in show else 0) for k in ALL) + '\n\n'
    s = s[:sec.start()] + new + s[sec.end():]
    open(os.path.join(WORK, workspace), 'w', encoding='utf-8').write(s)
    ini = ''
    for name, (x, y, w, h) in layout.items():
        ini += '[Window][%s]\nPos=%d,%d\nSize=%d,%d\nCollapsed=0\n\n' % (name, x, y, w, h)
    open(os.path.join(WORK, 'imgui.ini'), 'w').write(ini)

def shoot(name, workspace, show, layout, settle=14, actions=None, crop=None):
    prepare(workspace, show, layout)
    env = {'HOME': os.environ['HOME'], 'DISPLAY': ':0', 'PATH': '/usr/bin:/bin',
           'XAUTHORITY': os.environ.get('XAUTHORITY', os.path.expanduser('~/.Xauthority')),
           'XDG_RUNTIME_DIR': '/run/user/%d' % os.getuid()}
    p = subprocess.Popen([BIN, workspace], cwd=WORK, env=env, stdout=open('/tmp/viamd_shot.log', 'w'), stderr=subprocess.STDOUT)
    try:
        win = find_window(p.pid)
        if not win:
            print('no window'); return None
        time.sleep(3)
        raise_and_size(win)
        time.sleep(settle)
        raise_and_size(win)
        time.sleep(1.5)
        ax, ay, w, h = geom(win)
        if actions:
            actions(ax, ay)
        im = ImageGrab.grab(xdisplay=':0')
        box = (ax, ay, ax + w, ay + h) if not crop else (ax + crop[0], ay + crop[1], ax + crop[2], ay + crop[3])
        os.makedirs(OUT, exist_ok=True)
        path = os.path.join(OUT, name + '.png')
        im.crop(box).save(path)
        print('saved', path, box)
        return path
    finally:
        p.terminate()
        try: p.wait(5)
        except Exception: p.kill()

if __name__ == '__main__':
    shoot('test_system', 'showcase_gui.via', ['System'], {'System': (40, 60, 900, 640)})

def key(name, shift=False):
    x11.XStringToKeysym.restype = ctypes.c_ulong
    x11.XKeysymToKeycode.restype = ctypes.c_ubyte
    kc = x11.XKeysymToKeycode(ctypes.c_void_p(dpy), ctypes.c_ulong(x11.XStringToKeysym(name.encode())))
    sk = x11.XKeysymToKeycode(ctypes.c_void_p(dpy), ctypes.c_ulong(x11.XStringToKeysym(b'Shift_L')))
    if shift: xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), sk, 1, 0)
    xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), kc, 1, 0); x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.1)
    xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), kc, 0, 0)
    if shift: xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), sk, 0, 0)
    x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.5)

SE = (10, 330, 440, 560)

def evaluate(ax, ay, wait=40):
    x, y, w, h = SE
    click(ax + x + w // 2, ay + y + h - 40)
    key('Return', shift=True)
    time.sleep(wait)
    click(ax + x + w - 14, ay + y + 12)   # close the editor again
    time.sleep(2)

def session(workspace, show, layout, steps, settle=22):
    """steps: ('wait', s) | ('click', x, y) | ('rclick', x, y) | ('key', name) | ('eval', wait) | ('cap', name, crop|None) | ('away',)"""
    prepare(workspace, show, layout)
    env = {'HOME': os.environ['HOME'], 'DISPLAY': ':0', 'PATH': '/usr/bin:/bin',
           'XAUTHORITY': os.environ.get('XAUTHORITY', os.path.expanduser('~/.Xauthority')),
           'XDG_RUNTIME_DIR': '/run/user/%d' % os.getuid()}
    p = subprocess.Popen([BIN, workspace], cwd=WORK, env=env, stdout=open('/tmp/viamd_shot.log', 'w'), stderr=subprocess.STDOUT)
    saved = []
    try:
        win = find_window(p.pid)
        if not win:
            print('no window'); return saved
        time.sleep(3); raise_and_size(win); time.sleep(settle); raise_and_size(win); time.sleep(1.5)
        ax, ay, w, h = geom(win)
        for st in steps:
            k = st[0]
            if k == 'wait': time.sleep(st[1])
            elif k == 'click': click(ax + st[1], ay + st[2])
            elif k == 'rclick': click(ax + st[1], ay + st[2], 3)
            elif k == 'dbl': dclick(ax + st[1], ay + st[2])
            elif k == 'move': move(ax + st[1], ay + st[2])
            elif k == 'key': key(st[1])
            elif k == 'away': move(ax + w - 30, ay + h - 30)
            elif k == 'eval':
                x, y, ww, hh = SE
                click(ax + x + ww // 2, ay + y + hh - 40); key('Return', shift=True); time.sleep(st[1])
            elif k == 'cap':
                im = ImageGrab.grab(xdisplay=':0')
                c = st[2]
                box = (ax, ay, ax + w, ay + h) if not c else (ax + c[0], ay + c[1], ax + c[2], ay + c[3])
                os.makedirs(OUT, exist_ok=True)
                path = os.path.join(OUT, st[1] + '.png'); im.crop(box).save(path); saved.append(path); print('saved', path)
        return saved
    finally:
        p.terminate()
        try: p.wait(5)
        except Exception: p.kill()

def dclick(x, y):
    x11.XWarpPointer(ctypes.c_void_p(dpy), 0, x11.XDefaultRootWindow(ctypes.c_void_p(dpy)), 0, 0, 0, 0, x, y)
    x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.4)
    for _ in range(2):
        xtst.XTestFakeButtonEvent(ctypes.c_void_p(dpy), 1, 1, 0); x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.05)
        xtst.XTestFakeButtonEvent(ctypes.c_void_p(dpy), 1, 0, 0); x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.05)
    time.sleep(0.6)

_old_session = session
def session(workspace, show, layout, steps, settle=22):
    # steps ('dbl',x,y) and ('move',x,y) are handled by wrapping click helpers
    return _old_session(workspace, show, layout, steps, settle)
