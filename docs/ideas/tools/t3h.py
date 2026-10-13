import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
shot.SE = (10, 330, 440, 560)
script = 'd1 = distance(10,30);\nd2 = distance(5,40);'
layout = {'Script Editor': shot.SE}
ws = t.workspace(t2a.files, script, ['ScriptEditor'], [(0, 'd1')], [(0, 'd1')], reps=[t2a.r_prot, t2a.r_ain], frame=0, tl_subplots=1, dist_subplots=1)
steps = [('guard',), ('eval', 12),
  ('click', 364, 12), ('wait', 1), ('cap', 'menu_screenshot', (300, 0, 800, 110), 'noguard'), ('click', 900, 700),
  ('click', 566, 12), ('click', 556, 97), ('wait', 1), ('cap', 'menu_settings', (380, 0, 960, 380), 'noguard'), ('click', 1100, 700),
  ('click', 40, 366), ('wait', 1), ('cap', 'menu_script_file', (0, 330, 600, 700), 'noguard')]
t.run('t3h', ws, layout, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
