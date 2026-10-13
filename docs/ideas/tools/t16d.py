import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
ws = t.workspace(t2a.files, '', ['System'], [], [], reps=[t2a.r_prot], frame=0)
layout = {'System': (10, 30, 560, 800)}
steps = [('guard',), ('wait', 3), ('click', 100, 504), ('click', 100, 222), ('click', 100, 102), ('wait', 1), ('away',), ('wait', 1), ('guard',), ('rclick', 150, 373), ('wait', 1), ('guard',), ('cap', 'sys_series_menu', (0, 24, 760, 600))]
t.run('t16d', ws, layout, steps, '/tmp/shotbin/viamd', settle=15, links=t2a.links)
