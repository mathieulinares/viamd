# Tutorial 2 Voiceover Script

**Video:** `tutorial-2-aspirin-phospholipase.mp4`  
**Runtime:** 10:00  
**Read style:** Clear, measured instructional narration. The video contains no burned-in captions or cursor commentary. Timecodes are video time; leave the remaining time in each section for the molecular animation and plots to breathe.

## 00:00-00:40 | The study

In this tutorial, we will follow aspirin as it leaves the binding pocket of phospholipase A2. The trajectory starts from the crystal structure of the complex and uses a biased potential to pull the ligand away from the protein. We will use the motion to explore representations, dynamic selections, spatial selection, property calculations, filtering, and energy data in VIAMD. The goal is not only to make a clear view, but to connect what we see to measurements across the trajectory.

## 00:40-01:25 | Load the data

The study has three input files. The GRO file provides the molecular system, the XTC file contains its trajectory, and the EDR file contains energy terms. In a fresh session, open the GRO file first, then add the matching XTC trajectory. The System window is a useful check that the molecule and trajectory have loaded. We will add the EDR file later, when we are ready to compare interaction energies. The supplied Tutorial 2 workspace contains these same inputs and is a convenient way to inspect the finished analysis.

## 01:25-02:10 | First representations

Start with two representations. Show the protein as a cartoon and color it by secondary structure. This makes the overall fold and the binding pocket easy to recognize. Add a second representation for residue name AIN, the aspirin ligand. Use Spacefill and color by atom type so the ligand stands out from the protein. Rotate the view until the ligand and its surroundings are visible. These representations are a stable overview; we will add local detail without replacing them.

## 02:10-03:00 | Follow the trajectory

To keep the ligand centered while it moves, set the selection level to Component. Shift-click the aspirin, open its context menu, and choose Set as Centering Target. Enable Center Target in Operations, then play the trajectory. The view now follows the ligand as it is pulled out of the pocket. Repeat the operation for the protein, this time selecting by Instance. Comparing the two centering targets makes the relative motion easier to interpret: the protein is the stable reference, while the aspirin is the moving object.

## 03:00-04:15 | Dynamic representations

Return to the first frame before creating the local representations. Add the protein atoms within five angstroms of AIN as a Licorice representation, colored by residue index. This is the pocket neighborhood. Add water within four angstroms of AIN as a second Licorice representation, colored by atom type. First play with automatic updating disabled. Both selections describe the initial frame, so the pocket and nearby water remain tied to that starting geometry. Now return to the start and enable automatic updating for the water representation only. On playback, the pocket stays fixed while the water neighborhood follows aspirin. Finally, wrap each spatial expression in `residue(...)` when you want every atom of a residue included if any atom meets the distance condition.

## 04:15-05:00 | Tune the visual

Use the Visuals controls to make the molecular view readable rather than merely dramatic. Compare orthographic and perspective projection, and adjust the field of view if perspective is active. Tune the background and ambient occlusion so the pocket remains legible. Temporal anti-aliasing can smooth motion; motion blur and sharpening are optional companions, so judge them during playback. Double-click aspirin to focus the camera there, then adjust depth-of-field blur gently. Finish by checking exposure, gamma, and the simulation box. The best settings preserve the protein context and keep the ligand sharp.

## 05:00-05:55 | Select the pocket

There are two ways to store the pocket selection. For the first, use the protein-near-aspirin representation and set selection granularity to Component. Shift-click the residues around the ligand, rotating or moving along the trajectory when needed. Right-click the selection, send it to the Script Editor, store it at residue level, and name it `pocket_1`. The equivalent expression is `residue(protein and within(5.0, resname("AIN")))`. This expresses the same idea compactly and can be reused in scripts and representation filters.

## 05:55-06:40 | Grow a second selection

For an independent construction, hide water and return to the first frame. Select aspirin, open the selection context menu, and choose Grow. Set the mode to radial, use a five-angstrom radius, and apply it. Remove aspirin itself from the resulting selection, then create and store a new selection named `pocket_2`. Keeping both selections is useful: one comes from the filtered protein representation, while the other comes from a direct spatial operation. They can now be compared or reused in later calculations.

## 06:40-07:45 | Calculate properties

We will measure both ligand conformation and separation from the pocket. In the Script Editor, define `dih1` as the dihedral angle for aspirin atoms 8, 17, 18, and 20, restricted to residue AIN. Evaluate it and display it in the Timeline. Next define `d1` as the distance between `pocket_1` and AIN. Try the related expressions `distance_pair(pocket_1, resname("AIN"))`, `distance_pair(pocket_1, com(resname("AIN")))`, and `distance_pair(flatten(pocket_1), com(resname("AIN")))`. These answer different questions: minimum separation, pairwise distances, and distances to the ligand center of mass. Read the returned shape and units before interpreting the plots.

## 07:45-08:30 | Filter the bound interval

Keep `dih1` and `d1` visible, and hide the extra series if the plot is crowded. Enable the Timeline frame filter and move its range over the part of the trajectory where aspirin remains bound. In Distributions, add the filtered versions of `dih1` and `d1`. Compare the full-run and filtered distributions, then play through the selected interval. The bound ligand occupies a restricted conformation; after it leaves the pocket, its dihedral becomes freer to vary. The distance and dihedral together connect a structural change to the ligand's departure.

## 08:30-09:15 | Add interaction energies

Now load `aspirin-phospholipase.edr` through File, or drop it onto the VIAMD window. Read the protein-aspirin short-range Coulomb term with `attr("edr/coul_sr_protein_ain")` and the corresponding Lennard-Jones term with `attr("edr/lj_sr_protein_ain")`. Add them as `Tot = Coul + LJ`. The System window also lists the imported series, and you can drag those entries into a Timeline. Compare the energy traces with the distance and dihedral; treat them as complementary observables, not as a standalone explanation of the mechanism.

## 09:15-10:00 | Save and recap

Save the workspace as a VIA file. It preserves the input references, camera, representations, script, plots, and rendering settings, so the analysis can be reopened without rebuilding the view. Keep the workspace beside the GRO, XTC, and EDR files when using relative paths. We have followed the ligand from its binding pocket, built two pocket selections, compared conformation and separation, and added interaction energies. The workspace is now a reproducible starting point for exploring the trajectory further.