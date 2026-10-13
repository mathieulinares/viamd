import sys, os; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t17a, t17b
script = t17a.script.strip()
layout = {'Script Editor': (10, 690, 420, 200), 'Timelines': (10, 30, 1580, 330), 'Distributions': (10, 370, 1580, 320)}
ws = t17b.build([t17b.R_PROT], ['ScriptEditor', 'Timelines', 'Distributions'], script=script, tl=[(0, 'NA')], dist=[(0, 'NA'), (1, 'dNA'), (1, 'dwat'), (1, 'dprot'), (1, 'dmem')], tl_sub=1, dist_sub=2)
ws = ws.replace('Path=script/NA\nColor=0.258823544,0.807843208,0.890196145,1\nPlotType=line\nMarker=0\nMarkerSize=1\nBarWidth=1\nNumBins=128\nUseColormap=0',
                'Path=script/NA\nColor=0.258823544,0.807843208,0.890196145,1\nPlotType=scatter\nMarker=0\nMarkerSize=1\nBarWidth=1\nNumBins=128\nUseColormap=1')
ws = ws.replace('Path=script/NA\nColor=0.258823544,0.807843208,0.890196145,1\nPlotType=area\nMarker=0\nMarkerSize=1\nBarWidth=1\nNumBins=128\nUseColormap=0',
                'Path=script/NA\nColor=0.258823544,0.807843208,0.890196145,1\nPlotType=area\nMarker=0\nMarkerSize=1\nBarWidth=1\nNumBins=128\nUseColormap=1')
print(ws.count('UseColormap=1'))
steps = [('guard',), ('eval', 30), ('wait', 40), ('guard',), ('dbl', 800, 200), ('dbl', 700, 480), ('dbl', 700, 604), ('away',), ('wait', 3), ('guard',), ('cap', 't4_plots', (0, 24, 1600, 900))]
if __name__ == '__main__':
    t.run('t17e', ws, layout, steps, '/tmp/shotbin/viamd', settle=40, links=t17a.links)
