import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t
S = shot.SRC
links = [(S + '/aspirin-phospholipase.gro', 'aspirin-phospholipase.gro'), (S + '/aspirin-phospholipase.xtc', 'aspirin-phospholipase.xtc'),
         (S + '/aspirin-phospholipase.edr', 'aspirin-phospholipase.edr'), (S + '/aspirin-phospholipase.xtc.cache', 'aspirin-phospholipase.xtc.cache')]
files = [('MoleculeFile', './aspirin-phospholipase.gro'), ('TrajectoryFile', './aspirin-phospholipase.xtc'), ('EnergyFile', './aspirin-phospholipase.edr')]
layout = {'Representations': (10, 30, 430, 400), 'Animation': (10, 440, 430, 150)}
AIN = 'resname("AIN")'
r_prot = ('protein', 'protein', 4, 8)
r_ain = ('aspirin', AIN, 0, 1)
r_pocket = ('pocket', 'protein and within(5.0, %s)' % AIN, 1, 5, 1, '1,1,1,1', '1,1,1,1', 1)
r_water = ('water', 'water and within(4.0, %s)' % AIN, 1, 1, 1, '1,1,1,1', '1,1,1,1', 1)
r_pocket_res = ('pocket', 'residue(protein and within(5.0, %s))' % AIN, 1, 5, 1, '1,1,1,1', '1,1,1,1', 1)
r_water_res = ('water', 'residue(water and within(4.0, %s))' % AIN, 1, 1, 1, '1,1,1,1', '1,1,1,1', 1)
def go(name, reps, frame, shotname, windows=('Representations', 'Animation')):
    ws = t.workspace(files, '', list(windows), [], [], reps=reps, frame=frame)
    steps = [('guard',), ('scroll', 800, 450, 5), ('wait', 8), ('cap', shotname, (0, 24, 1600, 900), 'noguard')]
    t.run(name, ws, layout, steps, '/tmp/shotbin/viamd', settle=20, links=links)
if __name__ == '__main__':
    go('t2a', [r_prot, r_ain], 0, 't2_reps1')
    go('t2b', [r_prot, r_ain, r_pocket, r_water], 0, 't2_reps2')
