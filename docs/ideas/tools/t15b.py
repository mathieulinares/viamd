import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
layout = {}
ws = t.workspace(t2a.files, '', [], [], [], reps=[t2a.r_prot, t2a.r_ain], frame=0)
steps = [('guard',), ('wait', 5), ('cap', 'coord_widget', (50, 30, 260, 190)), ('click', 22, 12), ('wait', 2), ('guard',), ('cap', 'menu_file', (0, 0, 320, 150))]
t.run('t15b', ws, layout, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
