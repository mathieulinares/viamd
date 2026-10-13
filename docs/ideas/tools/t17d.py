import sys, os; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t17a, t17b
layout = {'Selection Query': (1060, 40, 520, 110), 'Selection Grow': (1060, 170, 300, 150)}
R_ION = ('ion', 'resname("SOD")', 0, 1, 1, '0.4,1,1,1', '1,1,1,1', 0)
R_MEM2 = ('membrane', 'resname("POPC")', 1, 0, 1, '0.3,1,1,1', '0.82,0.82,0.82,1', 0)
ws = t17b.build([R_MEM2, t17b.R_PROT, R_ION], [])
steps = [('guard',), ('click', 276, 12), ('click', 245, 91), ('type', 'resname("SOD")[103]'), ('key', 'Return'), ('wait', 1),
         ('click', 800, 700), ('key', 'F2'), ('wait', 2), ('click', 276, 12), ('click', 245, 135), ('wait', 1),
         ('click', 1565, 28), ('scroll', 800, 450, 2), ('wait', 1), ('away',), ('wait', 3), ('guard',), ('cap', 't4_loaded', (0, 24, 1600, 900))]
t.run('t17d', ws, layout, steps, '/tmp/shotbin/viamd', settle=40, links=t17a.links)
