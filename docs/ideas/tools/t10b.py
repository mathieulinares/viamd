import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
types = [('spacefill', 0, 1), ('licorice', 1, 1), ('ball-and-stick', 2, 1), ('ribbons', 3, 8), ('cartoon', 4, 8)]
for name, typ, cmap in types:
    ws = t.workspace(t2a.files, '', [], [], [], reps=[('protein', 'protein', typ, cmap)], frame=0)
    t.run('t10_' + name, ws, {}, [('guard',), ('scroll', 800, 450, 6), ('wait', 12), ('away',), ('wait', 3), ('cap', 'rep_' + name, (300, 150, 1300, 750))], '/tmp/shotbin/viamd', settle=20, links=t2a.links)
