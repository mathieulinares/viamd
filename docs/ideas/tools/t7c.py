import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a, t2b
shot.SE = (10, 330, 440, 560)
tl = [(0, 'dih1'), (1, 'd1')]
ws = t.workspace(t2a.files, t2b.script, ['ScriptEditor', 'Timelines', 'Distributions'], tl, tl, reps=t2b.reps, frame=0, tl_subplots=2, dist_subplots=2)
ws += "[TimelineView]\nFilterEnabled=1\nFilterFrames=0,150\nTemporalWindowEnabled=0\nTemporalWindowExtent=10\nViewFrames=0,600\n"
steps = [('guard',), ('eval', 40), ('wait', 8), ('click', 517, 410), ('wait', 1), ('cap', 't2_dist_props', (440, 360, 1200, 700), 'noguard'), ('click', 1400, 800),
         ('click', 605, 60), ('wait', 1), ('cap', 't2_tl_filter_menu', (440, 0, 1000, 300), 'noguard')]
t.run('t7c', ws, t2b.layout, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
