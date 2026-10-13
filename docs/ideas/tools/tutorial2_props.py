import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
from PIL import Image
shot.SE = (10, 330, 440, 560)
script = '''pocket_1 = residue(protein and within(5.0, resname("AIN")));
dih1 = dihedral(8,17,18,20);
d1 = distance(pocket_1, resname("AIN"));'''
layout = {'Script Editor': shot.SE, 'Timelines': (460, 24, 1130, 340), 'Distributions': (460, 374, 1130, 330)}
reps = [t2a.r_prot, t2a.r_ain, t2a.r_pocket_res]
def props(name, tl, shotname, dist_windows=('ScriptEditor', 'Timelines', 'Distributions')):
    ws = t.workspace(t2a.files, script, list(dist_windows), tl, tl, reps=reps, frame=0, tl_subplots=len(tl), dist_subplots=len(tl))
    steps = [('guard',), ('eval', 40), ('wait', 3)]
    for i in range(len(tl)):
        steps += [('dbl', 1000, 374 + 100 + 150 * i)]
    steps += [('away',), ('wait', 3), ('cap', shotname, (0, 24, 1600, 900), 'noguard')]
    t.run(name, ws, layout, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
if __name__ == '__main__':
    props('t2c', [(0, 'dih1')], 't2_prop_dih1')
    props('t2d', [(0, 'dih1'), (1, 'd1')], 't2_prop_dih1_d1')
