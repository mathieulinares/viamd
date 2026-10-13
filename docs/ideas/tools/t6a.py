import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
shot.SE = (10, 330, 440, 560)
script = '''Coul = attr("edr/coul_sr_protein_ain");
LJ = attr("edr/lj_sr_protein_ain");
Tot = Coul + LJ;'''
layout = {'Script Editor': shot.SE, 'Timelines': (460, 24, 1130, 340), 'Distributions': (460, 374, 1130, 330)}
tl = [(0, 'Tot'), (1, 'Coul'), (1, 'LJ')]
ws = t.workspace(t2a.files, script, ['ScriptEditor', 'Timelines', 'Distributions'], tl, tl, reps=[t2a.r_prot, t2a.r_ain], frame=0, tl_subplots=2, dist_subplots=2)
steps = [('guard',), ('eval', 20), ('wait', 3), ('dbl', 1000, 474), ('dbl', 1000, 624), ('away',), ('wait', 3), ('cap', 't2_energy', (0, 24, 1600, 900), 'noguard')]
t.run('t6a', ws, layout, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
# overview without windows
ws2 = t.workspace(t2a.files, '', [], [], [], reps=[t2a.r_prot, t2a.r_ain], frame=0)
t.run('t6b', ws2, {}, [('guard',), ('scroll', 800, 450, 3), ('wait', 8), ('away',), ('wait', 5), ('cap', 't2_overview', (0, 24, 1600, 900), 'noguard')], '/tmp/shotbin/viamd', settle=20, links=t2a.links)
