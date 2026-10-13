import sys, os; sys.path.insert(0, '/tmp')
import shot, tut_shots as t
S = os.path.expanduser('~/Desktop/bioexcel/tutorial-4')
links = [(S + '/step7_production_rest.gro', 'step7_production_rest.gro'), (S + '/ef.xtc', 'ef.xtc')]
files = [('MoleculeFile', './step7_production_rest.gro'), ('TrajectoryFile', './ef.xtc')]
script = '''NA = coord_z(resname("SOD"));
dNA = density_z(resname("SOD"));
dwat = density_z(water());
dprot = density_z(protein());
dmem = density_z(resname("POPC"));
'''
layout = {'Script Editor': (10, 330, 440, 560), 'Timelines': (460, 24, 1130, 400), 'Distributions': (460, 440, 1130, 440), 'Representations': (10, 30, 430, 290)}
tl = [(0, 'NA')]
dist = [(0, 'NA'), (1, 'dNA'), (1, 'dwat'), (1, 'dprot'), (1, 'dmem')]
if __name__ == '__main__':
    ws = t.workspace(files, script, ['ScriptEditor', 'Timelines', 'Distributions', 'Representations'], tl, dist,
                     reps=[('protein', 'protein', 4, 6)], frame=0, tl_subplots=1, dist_subplots=2)
    steps = [('guard',), ('eval', 90), ('wait', 20), ('guard',), ('cap', 't4_probe', (0, 24, 1600, 900))]
    t.run('t17a', ws, layout, steps, '/tmp/shotbin/viamd', settle=60, links=links)
