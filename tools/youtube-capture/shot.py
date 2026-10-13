#!/usr/bin/env python3
"""Screenshot helper for the wiki: launch VIAMD with a workspace and a layout, raise it, grab its window."""
import ctypes, ctypes.util, os, re, shutil, subprocess, sys, tempfile, textwrap, threading, time
from PIL import Image, ImageDraw, ImageFont, ImageGrab

SRC = os.path.expanduser('~/Desktop/bioexcel/aspirin')
WORK = '/tmp/shotwork'
BIN = '/tmp/shotbin/viamd'
OUT = '/tmp/shots'
W, H, X0, Y0 = 1600, 900, 100, 60

x11 = ctypes.CDLL('libX11.so.6')
xtst = ctypes.CDLL('libXtst.so.6')
x11.XInitThreads()
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

def drag(x0, y0, x1, y1, duration=0.5, steps=20, button=1):
    root = x11.XDefaultRootWindow(ctypes.c_void_p(dpy))
    x11.XWarpPointer(ctypes.c_void_p(dpy), 0, root, 0, 0, 0, 0, int(x0), int(y0))
    x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.2)
    xtst.XTestFakeButtonEvent(ctypes.c_void_p(dpy), button, 1, 0)
    x11.XFlush(ctypes.c_void_p(dpy))
    try:
        for i in range(1, steps + 1):
            t = i / steps
            x = round(x0 + (x1 - x0) * t)
            y = round(y0 + (y1 - y0) * t)
            x11.XWarpPointer(ctypes.c_void_p(dpy), 0, root, 0, 0, 0, 0, x, y)
            x11.XFlush(ctypes.c_void_p(dpy))
            time.sleep(duration / steps)
    finally:
        xtst.XTestFakeButtonEvent(ctypes.c_void_p(dpy), button, 0, 0)
        x11.XFlush(ctypes.c_void_p(dpy))

def perform_input_step(step, ax, ay):
    k = step[0]
    if k == 'click':
        x, y = ax + step[1], ay + step[2]; click(x, y); return x, y
    if k == 'rclick':
        x, y = ax + step[1], ay + step[2]; click(x, y, 3); return x, y
    if k == 'dbl':
        x, y = ax + step[1], ay + step[2]; dclick(x, y); return x, y
    if k == 'move':
        x, y = ax + step[1], ay + step[2]; move(x, y); return None
    if k == 'drag':
        x0, y0 = ax + step[1], ay + step[2]
        x1, y1 = ax + step[3], ay + step[4]
        drag(x0, y0, x1, y1, step[5], step[6], step[7]); return x1, y1
    if k == 'scroll':
        x, y = ax + step[1], ay + step[2]
        button = 4 if step[3] == 'up' else 5
        for _ in range(step[4]): click(x, y, button)
        return x, y
    if k == 'key':
        key(step[1], shift=step[2] if len(step) > 2 else False, altgr=step[3] if len(step) > 3 else False)
        return None
    if k == 'type':
        symbols = {'(': 'parenleft', ')': 'parenright', '"': 'quotedbl', ',': 'comma', ' ': 'space', '.': 'period', '/': '7', '-': 'minus', '_': 'underscore', '[': 'bracketleft', ']': 'bracketright', '{': 'braceleft', '}': 'braceright', ':': 'colon'}
        for ch in step[1]:
            if ch in '[]{}': key(symbols[ch], altgr=True)
            elif ch in symbols: key(symbols[ch], shift=ch in '()"_:/')
            else: key(ch.lower(), shift=ch.isupper())
        return None
    if k == 'eval':
        x, y, w, h = SE
        click(ax + x + w // 2, ay + y + h - 40)
        key('Return', shift=True)
        time.sleep(step[1])
        return None
    if k == 'away':
        move(ax + W - 30, ay + H - 30); return None
    raise ValueError('not an input action: %r' % (k,))

def pointer_position():
    D = ctypes.c_void_p(dpy)
    root = x11.XDefaultRootWindow(D)
    root_return, child = ctypes.c_ulong(), ctypes.c_ulong()
    root_x, root_y, win_x, win_y = ctypes.c_int(), ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
    mask = ctypes.c_uint()
    if x11.XQueryPointer(D, ctypes.c_ulong(root), ctypes.byref(root_return), ctypes.byref(child), ctypes.byref(root_x), ctypes.byref(root_y), ctypes.byref(win_x), ctypes.byref(win_y), ctypes.byref(mask)):
        return root_x.value, root_y.value
    return None

def draw_record_overlay(frame, caption, cursor, click_pos=None):
    frame = frame.convert('RGBA')
    overlay = Image.new('RGBA', frame.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    width, height = frame.size
    if caption:
        bar_h = min(104, max(72, height // 8))
        draw.rectangle((0, height - bar_h, width, height), fill=(0, 0, 0, 190))
        font_size = max(18, height // 32)
        try:
            font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', font_size)
        except OSError:
            font = ImageFont.load_default()
        wrap_width = max(36, int(width / max(1, font_size * 0.62)))
        draw.multiline_text((36, height - bar_h + 12), textwrap.fill(caption, width=wrap_width), font=font,
                            fill=(255, 255, 255, 255), spacing=5)
    if click_pos:
        x, y = click_pos
        draw.ellipse((x - 16, y - 16, x + 16, y + 16), outline=(255, 210, 40, 240), width=4)
    if cursor:
        x, y = cursor
        points = [(x, y), (x + 2, y + 27), (x + 9, y + 20), (x + 16, y + 34),
                  (x + 22, y + 31), (x + 15, y + 17), (x + 26, y + 16)]
        draw.polygon(points, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))
    return Image.alpha_composite(frame, overlay).convert('RGB')

def encode_jpeg_sequence(pattern, frame_count, fps, output_path, bitrate_kbps=6000):
    gst = shutil.which('gst-launch-1.0')
    if not gst:
        raise RuntimeError('GStreamer gst-launch-1.0 is required for MP4 output')
    caps = f'caps=image/jpeg,framerate=(fraction){fps}/1'
    raw_caps = f'video/x-raw,format=I420,framerate={fps}/1'
    command = [gst, '-q', '-e', 'multifilesrc', f'location={pattern}', 'index=0', f'stop-index={frame_count}', caps,
        '!', 'jpegdec', '!', 'videoconvert', '!', raw_caps, '!', 'x264enc', 'tune=zerolatency',
        'speed-preset=ultrafast', f'bitrate={bitrate_kbps}', '!', 'mp4mux', '!', 'filesink', f'location={output_path}']
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError('GStreamer MP4 encoding failed: ' + result.stderr.strip()[-2000:])

def record_video(name, duration, fps, crop, subtitle, events, ax, ay, w, h, output_size=None, overlay=True, bitrate_kbps=6000, temp_dir=None):
    frame_count = int(round(duration * fps))
    box = (ax, ay, ax + w, ay + h) if crop is None else (ax + crop[0], ay + crop[1], ax + crop[2], ay + crop[3])
    width, height = box[2] - box[0], box[3] - box[1]
    if width <= 0 or height <= 0:
        raise ValueError('record crop must have positive width and height')
    os.makedirs(OUT, exist_ok=True)
    output_path = os.path.join(OUT, name + '.mp4')
    started = time.monotonic()
    event_state = {'caption': subtitle, 'click_point': None, 'click_time': -1.0, 'error': None}
    event_lock = threading.Lock()

    def dispatch_events():
        try:
            for at, event in events:
                delay = started + at - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                if event[0] == 'caption':
                    with event_lock:
                        event_state['caption'] = event[1]
                else:
                    point = perform_input_step(event, ax, ay)
                    if point:
                        with event_lock:
                            event_state['click_point'] = point
                            event_state['click_time'] = time.monotonic()
        except BaseException as error:
            with event_lock:
                event_state['error'] = error

    event_thread = threading.Thread(target=dispatch_events, name='scenario-events', daemon=True)
    event_thread.start()
    with tempfile.TemporaryDirectory(prefix='viamd-video-', dir=temp_dir) as frames_dir:
        for frame_index in range(frame_count):
            elapsed = frame_index / fps
            delay = started + elapsed - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            frame = ImageGrab.grab(xdisplay=':0').crop(box).convert('RGB')
            if output_size:
                frame = frame.resize(tuple(output_size), Image.Resampling.LANCZOS)
            if overlay:
                cursor = pointer_position()
                with event_lock:
                    active_caption = event_state['caption']
                    click_point = event_state['click_point']
                    click_time = event_state['click_time']
                ring = None
                if cursor and box[0] <= cursor[0] < box[2] and box[1] <= cursor[1] < box[3]:
                    ring = (cursor[0] - box[0], cursor[1] - box[1]) if click_point and time.monotonic() - click_time < 0.45 else None
                    cursor = (cursor[0] - box[0], cursor[1] - box[1])
                    if output_size:
                        sx, sy = output_size[0] / width, output_size[1] / height
                        cursor = (round(cursor[0] * sx), round(cursor[1] * sy))
                        if ring: ring = (round(ring[0] * sx), round(ring[1] * sy))
                else:
                    cursor = None
                frame = draw_record_overlay(frame, active_caption, cursor, ring)
            frame.save(os.path.join(frames_dir, f'frame-{frame_index:06d}.jpg'), format='JPEG', quality=82)

        event_thread.join()
        if event_state['error']:
            raise RuntimeError(f'scenario event failed: {event_state["error"]}') from event_state['error']
        encode_jpeg_sequence(os.path.join(frames_dir, 'frame-%06d.jpg'), frame_count, fps, output_path, bitrate_kbps)
    print('saved video', output_path)
    return output_path

ALL = ['Timelines', 'Distributions', 'Representations', 'ScriptEditor', 'Animation', 'Attributes',
       'Contacts', 'System', 'DensityVolume', 'Ramachandran', 'ShapeSpace']

def prepare(workspace, show, layout, workspace_file=None):
    shutil.rmtree(WORK, ignore_errors=True); os.makedirs(WORK)
    for f in os.listdir(SRC):
        if f.startswith('aspirin-phospholipase') or f.endswith('.pdb'):
            os.symlink(os.path.join(SRC, f), os.path.join(WORK, f))
    s = open(workspace_file or os.path.join(SRC, workspace), encoding='utf-8').read()
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

def key(name, shift=False, altgr=False):
    x11.XStringToKeysym.restype = ctypes.c_ulong
    x11.XKeysymToKeycode.restype = ctypes.c_ubyte
    kc = x11.XKeysymToKeycode(ctypes.c_void_p(dpy), ctypes.c_ulong(x11.XStringToKeysym(name.encode())))
    sk = x11.XKeysymToKeycode(ctypes.c_void_p(dpy), ctypes.c_ulong(x11.XStringToKeysym(b'Shift_L')))
    ak = x11.XKeysymToKeycode(ctypes.c_void_p(dpy), ctypes.c_ulong(x11.XStringToKeysym(b'ISO_Level3_Shift')))
    if shift: xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), sk, 1, 0)
    if altgr and ak: xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), ak, 1, 0)
    xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), kc, 1, 0); x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.1)
    xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), kc, 0, 0)
    if altgr and ak: xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), ak, 0, 0)
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

def session(workspace, show, layout, steps, settle=22, keep_open=False, workspace_file=None):
    """steps: ('wait', s) | ('click', x, y) | ('rclick', x, y) | ('key', name) | ('eval', wait) | ('cap', name, crop|None) | ('away',)"""
    prepare(workspace, show, layout, workspace_file)
    env = {'HOME': os.environ['HOME'], 'DISPLAY': ':0', 'PATH': '/usr/bin:/bin',
           'XAUTHORITY': os.environ.get('XAUTHORITY', os.path.expanduser('~/.Xauthority')),
           'XDG_RUNTIME_DIR': '/run/user/%d' % os.getuid()}
    command = [BIN] if workspace_file else [BIN, workspace]
    p = subprocess.Popen(command, cwd=WORK, env=env, stdout=open('/tmp/viamd_shot.log', 'w'), stderr=subprocess.STDOUT)
    saved = []
    try:
        win = find_window(p.pid)
        if not win:
            print('no window'); return saved
        time.sleep(3); raise_and_size(win); time.sleep(settle); raise_and_size(win); time.sleep(1.5)
        ax, ay, w, h = geom(win)
        if workspace_file:
            click(ax + 25, ay + 15)
            click(ax + 90, ay + 64)
            time.sleep(2)
            _control_key('l')
            perform_input_step(('type', os.path.join(WORK, workspace)), ax, ay)
            key('Return')
            time.sleep(settle)
            raise_and_size(win)
            ax, ay, w, h = geom(win)
        for st in steps:
            k = st[0]
            if k == 'wait': time.sleep(st[1])
            elif k == 'click': click(ax + st[1], ay + st[2])
            elif k == 'rclick': click(ax + st[1], ay + st[2], 3)
            elif k == 'dbl': dclick(ax + st[1], ay + st[2])
            elif k == 'move': move(ax + st[1], ay + st[2])
            elif k == 'drag': drag(ax + st[1], ay + st[2], ax + st[3], ay + st[4], st[5], st[6], st[7])
            elif k == 'scroll':
                button = 4 if st[3] == 'up' else 5
                for _ in range(st[4]): click(ax + st[1], ay + st[2], button)
            elif k == 'key': key(st[1], shift=st[2] if len(st) > 2 else False, altgr=st[3] if len(st) > 3 else False)
            elif k == 'type':
                SH = {'(': 'parenleft', ')': 'parenright', '"': 'quotedbl', ',': 'comma', ' ': 'space', '.': 'period', '/': '7', '-': 'minus', '_': 'underscore', '[': 'bracketleft', ']': 'bracketright', '{': 'braceleft', '}': 'braceright', ':': 'colon'}
                for ch in st[1]:
                    if ch in '[]{}': key(SH[ch], altgr=True)
                    elif ch in SH: key(SH[ch], shift=ch in '()"_:/')
                    else: key(ch.lower(), shift=ch.isupper())
            elif k == 'away': move(ax + w - 30, ay + h - 30)
            elif k == 'eval':
                x, y, ww, hh = SE
                click(ax + x + ww // 2, ay + y + hh - 40); key('Return', shift=True); time.sleep(st[1])
            elif k == 'record':
                _, name, duration, fps, crop, subtitle, events, output_size, overlay, bitrate_kbps, temp_dir = st
                saved.append(record_video(name, duration, fps, crop, subtitle, events, ax, ay, w, h, output_size, overlay, bitrate_kbps, temp_dir))
            elif k == 'cap':
                im = ImageGrab.grab(xdisplay=':0')
                c = st[2]
                box = (ax, ay, ax + w, ay + h) if not c else (ax + c[0], ay + c[1], ax + c[2], ay + c[3])
                os.makedirs(OUT, exist_ok=True)
                path = os.path.join(OUT, st[1] + '.png'); im.crop(box).save(path); saved.append(path); print('saved', path)
        if keep_open:
            try:
                input('Replay finished; VIAMD is open. Press Enter here to close it.\n')
            except EOFError:
                pass
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

def _control_key(name):
    ctrl = x11.XKeysymToKeycode(ctypes.c_void_p(dpy), ctypes.c_ulong(x11.XStringToKeysym(b'Control_L')))
    xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), ctrl, 1, 0); x11.XFlush(ctypes.c_void_p(dpy))
    key(name)
    xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), ctrl, 0, 0); x11.XFlush(ctypes.c_void_p(dpy))

_old_session = session
def session(workspace, show, layout, steps, settle=22, keep_open=False, workspace_file=None):
    # steps ('dbl',x,y) and ('move',x,y) are handled by wrapping click helpers
    return _old_session(workspace, show, layout, steps, settle, keep_open, workspace_file)
