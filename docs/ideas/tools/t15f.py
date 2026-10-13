import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
script = '''# Tutorial 2: aspirin in the pocket of a phospholipase.

# The pocket, selected by hand (Selection > Query, then right click > Script)
pocket_1 = residue({5,9,18,21:23,27:31,44,47:48,63,100});

# Dihedral angle of the aspirin
dih1 = dihedral(8, 17, 18, 20) in resname("AIN");

# Distance between the pocket and the aspirin
d1 = distance(pocket_1,resname("AIN"));
d2 = distance_pair(pocket_1,resname("AIN"));
d3 = distance_pair(pocket_1,com(resname("AIN")));
d4 = distance_pair(flatten(pocket_1),com(resname("AIN")));

# Energy between the protein and the aspirin, read from the .edr file
Coul = attr("edr/coul_sr_protein_ain");
LJ = attr("edr/lj_sr_protein_ain");
Tot = Coul + LJ;
'''
ws = t.workspace(t2a.files, script, ['Representations', 'ScriptEditor', 'Timelines', 'Distributions', 'Animation'],
                 [(0, 'dih1'), (1, 'd1'), (1, 'd3'), (1, 'Tot')], [(0, 'dih1'), (1, 'd1')],
                 reps=[t2a.r_prot, t2a.r_ain, t2a.r_pocket_res, t2a.r_water_res], frame=0)
open('/tmp/tutorial-2.via', 'w', encoding='utf-8').write(ws)
print(len(ws))
