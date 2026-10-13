import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t2a
RS = """[RenderSettings]
BackgroundColor=0.99613899,0.99613899,0.99613899
BackgroundIntensity=24
SsaoEnabled=1
SsaoStrength=5
SsaoTemporal=1
SsaoMode=1
TonemapEnabled=1
Tonemapper=3
TonemapExposure=1
TonemapGamma=2.20000005
DofEnabled=1
DofAperture=%s
FxaaEnabled=1
TaaEnabled=1
TaaJitter=1
TaaFeedbackMin=0.800000012
TaaFeedbackMax=0.949999988
MotionBlurEnabled=0
MotionBlurScale=1
SharpenEnabled=1
SharpenWeight=1
SimulationBoxEnabled=0
SimulationBoxColor=0,0,0,0.5

"""
def go(ap, shotname):
    ws = t.workspace(t2a.files, '', [], [], [], reps=[t2a.r_prot, t2a.r_ain, t2a.r_pocket_res], frame=0) + RS % ap
    steps = [('guard',), ('scroll', 800, 450, 5), ('dbl', 760, 414), ('wait', 10), ('away',), ('wait', 6), ('cap', shotname, (0, 24, 1600, 900), 'noguard')]
    t.run('t5a', ws, {'Representations': (10, 30, 430, 400)}, steps, '/tmp/shotbin/viamd', settle=20, links=t2a.links)
if __name__ == '__main__':
    go(3, 'polished')
