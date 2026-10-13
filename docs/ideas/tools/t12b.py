import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t8a
ws = t8a.make('', [], [], windows=(), reps=(t8a.protein, ('probes', 'resname("PFT")', 0, 1)))
steps = [('guard',), ('scroll', 800, 450, 3), ('wait', 20), ('away',), ('wait', 3), ('guard',), ('cap', 't3_overview', (0, 24, 1600, 900))]
t.run('t12b', ws, {}, steps, '/tmp/shotbin/viamd', settle=70, links=t8a.links)
