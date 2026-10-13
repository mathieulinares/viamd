import sys, os; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t17a, t17b
layout = {'Selection Query': (1060, 40, 520, 110), 'Selection Grow': (1060, 170, 300, 150)}
R_PROT = ('protein', 'protein', 4, 6)
R_NA = t17b.R_NA
ws = t17b.build([R_PROT, R_NA], [])
steps = [('guard',), ('click', 276, 12), ('click', 245, 91), ('type', 'resname("SOD")[103]'), ('key', 'Return'), ('wait', 1),
         ('click', 800, 700), ('key', 'F2'), ('wait', 2), ('scroll', 800, 450, 3), ('wait', 4), ('away',), ('wait', 2), ('guard',),
         ('cap', 't4_ion_probe', (0, 24, 1600, 900))]
t.run('t17c', ws, layout, steps, '/tmp/shotbin/viamd', settle=40, links=t17a.links)
