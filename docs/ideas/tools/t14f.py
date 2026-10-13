import sys; sys.path.insert(0, '/tmp')
import t14a
t14a.one('t14f', [('proteins', 'protein', 0, 7), ('lipids', 'resname("DPPC")', 0, 1, 1, '0.6,0.6,0.6,1')], ['Contacts'], {'Contacts': (460, 40, 1000, 640)},
    [('click', 580, 162), ('wait', 90), ('away',), ('wait', 3), ('guard',), ('cap', 'cg_contacts', (0, 24, 1600, 900))])
