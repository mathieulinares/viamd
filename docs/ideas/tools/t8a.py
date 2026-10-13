import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
S = '/home/mathieu-linares/Desktop/bioexcel/amyloid/'
links = [(S + 'amyloid-pftaa.gro', 'amyloid-pftaa.gro'), (S + 'amyloid-pftaa.xtc', 'amyloid-pftaa.xtc')]
files = [('MoleculeFile', './amyloid-pftaa.gro'), ('TrajectoryFile', './amyloid-pftaa.xtc')]
shot.SE = (10, 330, 440, 560)
layout = {'Script Editor': shot.SE, 'Timelines': (460, 24, 1130, 340), 'Distributions': (460, 374, 1130, 330)}
protein = ('protein', 'protein', 4, 8)
probes = ('probes', 'resname("PFT")', 1, 1)
def make(script, tl, dist=None, extra='', reps=(protein, probes), frame=0, windows=('ScriptEditor', 'Timelines', 'Distributions')):
    return t.workspace(files, script, list(windows), tl, dist if dist is not None else tl, reps=list(reps), frame=frame, tl_subplots=max([s for s, _ in tl] + [0]) + 1, dist_subplots=max([s for s, _ in (dist if dist is not None else tl)] + [0]) + 1) + extra
if __name__ == '__main__':
    script = 'd = distance_min(resname("PFT"), protein);'
    ws = make(script, [(0, 'd')])
    steps = [('guard',), ('eval', 120), ('wait', 5), ('away',), ('cap', 't3_probe', (0, 24, 1600, 900), 'noguard')]
    t.run('t8a', ws, layout, steps, '/tmp/shotbin/viamd', settle=90, links=links)
