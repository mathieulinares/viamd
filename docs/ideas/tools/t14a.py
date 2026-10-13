import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t
C = '/home/mathieu-linares/Downloads/M-4-900-300.tpr'
links = [(C, 'cg.tpr')]
files = [('MoleculeFile', './cg.tpr')]
def one(name, reps, windows, layout, steps, settle=40):
    ws = t.workspace(files, '', windows, [], [], reps=reps, frame=0)
    t.run(name, ws, layout, [('guard',)] + steps, '/tmp/shotbin/viamd', settle=settle, links=links)
if __name__ == '__main__':
    one('t14a', [('protein', 'protein', 0, 1), ('rest', 'not protein', 0, 1, 0)], ['System', 'Representations'],
        {'System': (10, 30, 1000, 470), 'Representations': (10, 510, 430, 380)},
        [('wait', 20), ('away',), ('wait', 3), ('guard',), ('cap', 'cg_probe', (0, 24, 1600, 900))])
