import sys, os, ctypes, time; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t17a, t17b
from shot import xtst, x11
orig = shot.click
def cclick(x, y, button=1):
    if button == 1 and getattr(shot, 'CTRL_BOX', None) and shot.CTRL_BOX[0] <= x <= shot.CTRL_BOX[2] and shot.CTRL_BOX[1] <= y <= shot.CTRL_BOX[3]:
        x11.XStringToKeysym.restype = ctypes.c_ulong; x11.XKeysymToKeycode.restype = ctypes.c_ubyte
        ck = x11.XKeysymToKeycode(ctypes.c_void_p(shot.dpy), ctypes.c_ulong(x11.XStringToKeysym(b'Control_L')))
        xtst.XTestFakeKeyEvent(ctypes.c_void_p(shot.dpy), ck, 1, 0); x11.XFlush(ctypes.c_void_p(shot.dpy)); time.sleep(0.2)
        orig(x, y, button)
        xtst.XTestFakeKeyEvent(ctypes.c_void_p(shot.dpy), ck, 0, 0); x11.XFlush(ctypes.c_void_p(shot.dpy))
    else:
        orig(x, y, button)
shot.click = cclick
shot.CTRL_BOX = (100 + 560, 60 + 125, 100 + 680, 60 + 143)   # screen coords of the Extent slider
layout = {'Selection Query': (1060, 40, 520, 110), 'Selection Grow': (460, 60, 300, 150)}
ws = t17b.build([t17b.R_PROT, t17b.R_NA], [])
steps = [('guard',), ('click', 276, 12), ('click', 245, 91), ('type', 'resname("SOD")[103]'), ('key', 'Return'), ('wait', 1),
         ('click', 800, 700), ('key', 'F2'), ('wait', 2), ('scroll', 800, 450, 5), ('wait', 1),
         ('click', 276, 12), ('click', 245, 113), ('wait', 1), ('click', 610, 106), ('click', 500, 155), ('wait', 1),
         ('click', 610, 134), ('type', '5'), ('key', 'Return'), ('wait', 1), ('click', 494, 160), ('wait', 1), ('move', 700, 700), ('wait', 2), ('guard',),
         ('cap', 't4_grow', (0, 24, 1600, 900))]
t.run('t17f', ws, layout, steps, '/tmp/shotbin/viamd', settle=40, links=t17a.links)
