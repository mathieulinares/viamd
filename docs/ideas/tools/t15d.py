import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
from shot import xtst, x11
import ctypes, time
orig = shot.click
def sclick(x, y, button=1):
    if button == 1 and 700 <= x <= 900 and 180 <= y <= 560:
        x11.XStringToKeysym.restype = ctypes.c_ulong; x11.XKeysymToKeycode.restype = ctypes.c_ubyte
        sk = x11.XKeysymToKeycode(ctypes.c_void_p(shot.dpy), ctypes.c_ulong(x11.XStringToKeysym(b'Shift_L')))
        xtst.XTestFakeKeyEvent(ctypes.c_void_p(shot.dpy), sk, 1, 0); x11.XFlush(ctypes.c_void_p(shot.dpy)); time.sleep(0.2)
        orig(x, y, button)
        xtst.XTestFakeKeyEvent(ctypes.c_void_p(shot.dpy), sk, 0, 0); x11.XFlush(ctypes.c_void_p(shot.dpy))
    else:
        orig(x, y, button)
shot.click = sclick
ws = t.workspace(t2a.files, '', [], [], [], reps=[t2a.r_ain], frame=0)
steps = [('guard',), ('wait', 3), ('scroll', 778, 436, 12), ('wait', 2),
         ('click', 740, 282), ('click', 656, 412), ('click', 692, 346), ('wait', 1), ('cap', 'sel3', (0, 24, 1600, 900)),
         ('rclick', 692, 346), ('wait', 1), ('move', 720, 360), ('wait', 1), ('guard',), ('cap', 'ctx3', (0, 24, 1600, 900))]
t.run('t15d', ws, {}, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
