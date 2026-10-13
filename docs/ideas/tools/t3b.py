import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
reps = [t2a.r_prot, t2a.r_ain]
def one(name, windows, layout, steps):
    ws = t.workspace(t2a.files, '', windows, [], [], reps=reps, frame=0)
    t.run(name, ws, layout, [('guard',)] + steps, '/tmp/shotbin/viamd', settle=18, links=t2a.links)
if __name__ == "__main__":
    one('t3c', ['Contacts'], {'Contacts': (460, 40, 700, 560)},
        [('click', 580, 162), ('wait', 8), ('away',), ('wait', 2), ('cap', 'contacts', (450, 30, 1170, 610), 'noguard')])
    one('t3d', [], {'Structure Export': (460, 40, 560, 220)},
        [('click', 174, 12), ('click', 240, 212), ('away',), ('wait', 2), ('cap', 'structure_export', (440, 24, 1100, 300), 'noguard')])
    one('t3e', [], {'Script Reference': (460, 40, 1000, 640)},
        [('click', 174, 12), ('click', 230, 128), ('away',), ('wait', 3), ('cap', 'script_reference', (440, 24, 1480, 700), 'noguard')])

