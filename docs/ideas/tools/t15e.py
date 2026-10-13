import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
ws = t.workspace(t2a.files, '', [], [], [], reps=[t2a.r_prot, t2a.r_ain, t2a.r_pocket_res], frame=0)
steps = [('guard',), ('scroll', 800, 450, 5), ('click', 276, 12), ('click', 245, 91),
         ('type', 'residue(protein and within(5.0, resname("AIN")))'), ('key', 'Return'), ('wait', 2),
         ('away',), ('wait', 2), ('guard',), ('cap', 'pocket_sel', (0, 24, 1600, 900))]
t.run('t15e', ws, {}, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
