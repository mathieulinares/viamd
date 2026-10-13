import sys, os; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t17a
CAM = '''[Operations]
Recenter=0
ApplyPbc=1
UnwrapStructures=1

'''
def build(reps, windows=(), script='', tl=(), dist=(), frame=1000, tl_sub=1, dist_sub=1, cam=True):
    ws = t.workspace(t17a.files, script, list(windows), list(tl), list(dist), reps=reps, frame=frame, tl_subplots=tl_sub, dist_subplots=dist_sub)
    return ws + (CAM if cam else '')
R_PROT = ('protein', 'protein', 4, 6)
R_MEM = ('membrane', 'resname("POPC")', 1, 1, 1, '1,1,1,1', '1,1,1,1', 0)
R_NA = ('na', 'resname("SOD")[103]', 0, 0, 1, '1,1,1,1', '1,0,0,1', 0)
if __name__ == '__main__':
    ws = build([R_MEM, R_PROT, R_NA], [])
    steps = [('guard',), ('scroll', 800, 410, 4), ('wait', 6), ('away',), ('cap', 't4_probe_b', (0, 24, 1600, 900))]
    t.run('t17b', ws, {}, steps, '/tmp/shotbin/viamd', settle=40, links=t17a.links)
