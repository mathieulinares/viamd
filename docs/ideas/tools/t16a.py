import sys, os, glob; sys.path.insert(0, '/tmp')
import shot, tut_shots as t
D = os.path.expanduser('~/Downloads')
h5 = glob.glob(D + '/biphenyl-scf-*.h5')[0]
def rep(bo):
    return t.rep_block('rep', 'all', 2, 1) .replace('DynamicEval=0', 'BondOrders=%d\nDynamicEval=0' % bo)
for bo, name in ((1, 'bo_on'), (0, 'bo_off')):
    ws = t.workspace([('MoleculeFile', './mol.h5')], '', [], [], [], reps=[rep(bo)], frame=0)
    steps = [('guard',), ('scroll', 800, 450, 3), ('wait', 4), ('guard',), ('cap', name, (400, 150, 1200, 800), 'noguard')]
    t.run('t16a_' + name, ws, {}, steps, '/tmp/shotbin/viamd', settle=12, links=[(h5, 'mol.h5')])
