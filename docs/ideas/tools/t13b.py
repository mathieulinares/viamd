import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t
H = '/home/mathieu-linares/Downloads/hMff_60SDS_monomer_CHARMM36m_OPC_37C_replica0.tpr'
links = [(H, 'hmff.tpr')]
files = [('MoleculeFile', './hmff.tpr')]
def one(name, reps, windows, layout, steps):
    ws = t.workspace(files, '', windows, [], [], reps=reps, frame=0)
    t.run(name, ws, layout, [('guard',)] + steps, '/tmp/shotbin/viamd', settle=40, links=links)
if __name__ == '__main__':
    # A: coloring by the charge of the tpr
    one('t13b', [('charge', 'not water', 0, 9)], ['Representations'], {'Representations': (10, 30, 430, 420)},
        [('scroll', 800, 450, 4), ('wait', 15), ('away',), ('wait', 3), ('guard',), ('cap', 'tpr_charge', (0, 24, 1600, 900))])
    # B: contacts with the energy from the force field of the tpr
    one('t13c', [('protein', 'protein', 4, 8), ('SDS', 'not protein and not water', 1, 1)], ['Contacts'], {'Contacts': (460, 40, 1000, 640)},
        [('click', 580, 162), ('wait', 60), ('away',), ('wait', 3), ('guard',), ('cap', 'tpr_contacts', (0, 24, 1600, 900))])
