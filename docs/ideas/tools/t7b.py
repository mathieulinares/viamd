import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a, t2b
shot.SE = (10, 330, 440, 560)
tl = [(0, 'dih1'), (1, 'd1')]
ws = t.workspace(t2a.files, t2b.script, ['ScriptEditor', 'Timelines', 'Distributions'], tl, tl, reps=t2b.reps, frame=0, tl_subplots=2, dist_subplots=2)
extra = ''.join(t.series_block('DistributionSeries', sp, ident, col, 'area').replace('Source=script\n', 'Source=script_filtered\n')
                for sp, ident, col in [(0, 'dih1', '1,0.6,0.2,1'), (1, 'd1', '1,0.6,0.2,1')])
i = ws.index('[Representation]')
ws = ws[:i] + extra + ws[i:]
ws += "[TimelineView]\nFilterEnabled=1\nFilterFrames=0,150\nTemporalWindowEnabled=0\nTemporalWindowExtent=10\nViewFrames=0,600\n"
steps = [('guard',), ('eval', 40), ('wait', 5), ('dbl', 1000, 474), ('dbl', 1000, 624), ('away',), ('wait', 3), ('cap', 't2_filter', (0, 24, 1600, 900), 'noguard')]
t.run('t7b', ws, t2b.layout, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
