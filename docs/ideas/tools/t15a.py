import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
shot.SE = (10, 330, 440, 560)
script = 'd1 = distance(10,30);\nd2 = distance(5,40);'
layout = {'Script Editor': shot.SE, 'Timelines': (460, 24, 1130, 340), 'Property Export': (460, 380, 480, 175)}
tl = [(0, 'd1'), (0, 'd2')]
ws = t.workspace(t2a.files, script, ['ScriptEditor', 'Timelines'], tl, tl, reps=[], frame=0, tl_subplots=1, dist_subplots=1)
steps = [('guard',), ('eval', 15), ('click', 40, 366), ('click', 50, 439), ('wait', 2), ('away',), ('wait', 2), ('guard',), ('cap', 'prop_export', (455, 378, 945, 562))]
t.run('t15a', ws, layout, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
