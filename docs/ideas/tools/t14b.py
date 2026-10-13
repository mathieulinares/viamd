import sys; sys.path.insert(0, '/tmp')
import t14a
one = t14a.one
one('t14b', [('proteins', 'protein', 0, 7), ('lipids', 'resname("DPPC")', 0, 1, 1, '0.6,0.6,0.6,1'), ('ions', 'resname("NA+") or resname("CL-")', 0, 1)], ['Representations'],
    {'Representations': (10, 30, 430, 300)},
    [('scroll', 800, 450, 1), ('wait', 20), ('away',), ('wait', 3), ('guard',), ('cap', 'cg_view', (0, 24, 1600, 900))])
one('t14c', [('proteins', 'protein', 4, 8)], ['Representations'], {'Representations': (10, 30, 430, 300)},
    [('wait', 15), ('away',), ('wait', 3), ('guard',), ('cap', 'cg_cartoon', (0, 24, 1600, 900))])
one('t14e', [], ['System'], {'System': (10, 30, 1000, 560)},
    [('wait', 10), ('away',), ('wait', 3), ('guard',), ('cap', 'cg_system', (0, 24, 1600, 900))])
