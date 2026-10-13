import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
shot.SE = (10, 330, 440, 560)
script = 'd1 = distance(10,30);\nd2 = distance(5,40);'
layout = {'Script Editor': shot.SE, 'Timelines': (460, 24, 1130, 340)}
ws = t.workspace(t2a.files, script, ['ScriptEditor', 'Timelines'], [(0, 'd1')], [(0, 'd1')], reps=[t2a.r_prot, t2a.r_ain], frame=0, tl_subplots=1, dist_subplots=1)
steps = [('guard',), ('eval', 12),
  ('click', 364, 12), ('wait', 1), ('cap', 'menu_screenshot', (0, 0, 700, 300), 'noguard'), ('click', 700, 700),
  ('click', 566, 12), ('wait', 1), ('cap', 'menu_settings', (400, 0, 900, 400), 'noguard'), ('click', 700, 700),
  ('click', 40, 396), ('wait', 1), ('cap', 'menu_script_file', (0, 330, 600, 700), 'noguard')]
t.run('t3g', ws, layout, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
