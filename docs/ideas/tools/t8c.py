import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t8a
script = open('/tmp/tut3_script.txt').read()
dist = [(0, 'dih1'), (0, 'dih2'), (0, 'dih3'), (0, 'dih4'), (1, 'dih_center_ads'), (1, 'dih_center_free'), (2, 'p_ads'), (2, 'p_free')]
ws = t8a.make(script, [], dist, windows=('ScriptEditor', 'Distributions'))
layout = {'Script Editor': shot.SE, 'Distributions': (460, 24, 1130, 780)}
steps = [('guard',), ('eval', 300), ('wait', 5), ('dbl', 1000, 140), ('dbl', 1000, 400), ('dbl', 1000, 650), ('away',), ('wait', 3), ('cap', 't3_dihedrals', (0, 24, 1600, 900))]
t.run('t8c', ws, layout, steps, '/tmp/shotbin/viamd', settle=90, links=t8a.links)
