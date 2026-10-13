import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
layout = {'Selection Query': (460, 60, 520, 110), 'Selection Grow': (460, 60, 300, 150)}
def run(name, steps):
    ws = t.workspace(t2a.files, '', [], [], [], reps=[t2a.r_prot, t2a.r_ain], frame=0)
    t.run(name, ws, layout, [('guard',), ('scroll', 800, 450, 5)] + steps, '/tmp/shotbin/viamd', settle=18, links=t2a.links)
C = (0, 24, 1600, 800)
if __name__ == "__main__":
    run('t4b', [('click', 276, 12), ('click', 245, 91), ('type', 'protein and within(5.0, resname("AIN"))'), ('wait', 2), ('cap', 'sel_query', C, 'noguard')])
    run('t4c', [('click', 276, 12), ('click', 245, 91), ('type', 'resname("AIN")'), ('key', 'Return'), ('wait', 1),
                ('click', 276, 12), ('click', 245, 113), ('wait', 1), ('click', 610, 106), ('wait', 1), ('cap', 'sel_grow_probe', C, 'noguard')])
