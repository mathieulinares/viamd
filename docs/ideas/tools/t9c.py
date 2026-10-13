import sys; sys.path.insert(0, '/tmp')
import shot, tut_shots as t, t8a
full = open('/tmp/tut3_script.txt').read()
# keep what the SDF and shape space need
keep = [l for l in full.split('\n') if not l.startswith('dih') and not l.startswith('p_') and 'abs(' not in l and not l.startswith('d_') and not l.startswith(' ')]
script = '\n'.join(keep)
layer = """[DensityVolume]
LayerCount=3
ActiveLayer=0
ShowCoordinateSystem=1

[DensityVolumeLayer]
Visible=1
PropertySource=script
PropertyPath=script/v
DvrEnabled=0
DvrColormap=5
DvrAlphaScale=1
DvrRange=0,1
IsoEnabled=1
IsoCount=2
IsoValue0=0.2
IsoColor0=0.9,0.1,0.1,0.25
IsoValue1=0.8
IsoColor1=0.9,0.1,0.1,0.6
ClipMin=0,0,0
ClipMax=1,1,1
LegendEnabled=1
LegendCheckerboard=1
LegendColormapMode=2
ResolutionScale=2
ClipVolumeColor=1,0,0,1
BoundingBoxColor=0,0,0,1
ShowBoundingBox=1
ShowReferenceStructures=1
ShowReferenceEnsemble=0
RepType=2
RepColorMapping=1
RepParam=1,1,1,1
RepColor=1,1,1,1

[DensityVolumeLayer]
Visible=1
PropertySource=script
PropertyPath=script/v
DvrEnabled=0
DvrColormap=5
DvrAlphaScale=1
DvrRange=0,1
IsoEnabled=1
IsoCount=2
IsoValue0=1
IsoColor0=0.1,0.2,0.9,0.3
IsoValue1=3
IsoColor1=0.1,0.2,0.9,1
ClipMin=0,0,0
ClipMax=1,1,1
LegendEnabled=1
LegendCheckerboard=1
LegendColormapMode=2
ResolutionScale=2
ClipVolumeColor=1,0,0,1
BoundingBoxColor=0,0,0,1
ShowBoundingBox=0
ShowReferenceStructures=1
ShowReferenceEnsemble=0
RepType=2
RepColorMapping=1
RepParam=1,1,1,1
RepColor=1,1,1,1

[DensityVolumeLayer]
Visible=1
PropertySource=script
PropertyPath=script/v
DvrEnabled=0
DvrColormap=5
DvrAlphaScale=1
DvrRange=0,1
IsoEnabled=1
IsoCount=2
IsoValue0=0.05
IsoColor0=0.1,0.7,0.2,0.3
IsoValue1=0.1
IsoColor1=0.1,0.7,0.2,1
ClipMin=0,0,0
ClipMax=1,1,1
LegendEnabled=1
LegendCheckerboard=1
LegendColormapMode=2
ResolutionScale=2
ClipVolumeColor=1,0,0,1
BoundingBoxColor=0,0,0,1
ShowBoundingBox=0
ShowReferenceStructures=1
ShowReferenceEnsemble=0
RepType=2
RepColorMapping=1
RepParam=1,1,1,1
RepColor=1,1,1,1

[ShapeSpace]
Filter=all
PropertyName=shape

[ShapeSpaceLayout]
NumSubplots=1

[ShapeSpaceSeries]
Subplot=0
Source=script
Variant=values
Path=script/s_ads
Color=1,0.4,0.1,1
PlotType=scatter
Marker=0
MarkerSize=6
BarWidth=1
NumBins=128
UseColormap=0
Colormap=5
ColormapAlpha=1
ShowPoints=1
PointsAll=1
IsoLines=0
IsoLevels=1
DensityMap=0
IsoCount=5
DensityColormap=4
DensityAlpha=0.6
Smoothing=2

"""
ws = t8a.make(script, [], [], windows=('ScriptEditor', 'DensityVolume', 'ShapeSpace'))
i = ws.index('[Representation]')
ws = ws[:i] + layer + ws[i:]
layout = {'Script Editor': shot.SE, 'Density Volume': (460, 24, 760, 700), 'Shapespace': (1230, 24, 360, 400)}
steps = [('guard',), ('eval', 240), ('wait', 90), ('guard',), ('cap', 't3_sdf2', (0, 24, 1600, 800))]
if __name__ == '__main__':
    print(script)
    t.run('t9c', ws, layout, steps, '/tmp/shotbin/viamd', settle=90, links=t8a.links)
