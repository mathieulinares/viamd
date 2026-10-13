# Wiki update: state and remaining work

Written on 2026-10-12, at the end of the first working session on the wiki.

## Where everything is

| What | Where |
|---|---|
| Wiki working copy | `~/viamd.wiki`, branch `docu` (remote `origin` is Robin's wiki, `fork` is `mathieulinares/viamd.wiki`) |
| Online preview | https://github.com/mathieulinares/viamd/wiki (the `docu` branch pushed to the `master` of the fork's wiki) |
| Robin's wiki | `scanberg/viamd.wiki`, not touched |
| Ideas and tools | `docs/ideas/` in the viamd repository, branch `docu` |
| Screenshot helper | `docs/ideas/tools/wiki_screenshots.py` |
| Video idea and list | `docs/ideas/tutorial-videos-and-session-replay.md` |

Update the preview: commit on `docu` in `~/viamd.wiki`, then `git push --force fork docu:master`.

## What is done

- **Structure:** clean page names, `_Sidebar`, new Home, stubs under the old numbered names (`0.-Building` to `7.-VIAMD-for-VeloxChem`) so old links keep working.
- **New pages:** Quick-tour, Working-with-series, Attributes, System-window, File-formats, Example-workspaces, Keyboard-and-mouse, Troubleshooting, Contributing-to-the-docs, Movie-maker with six subpages.
- **Rewritten from the current source:** Representations, Spatial-view, Rendering, Animation (Operations), Timeline, Distributions, Ramachandran, Shape-space, Density-volume, Script-editor details, Install, Datasets, VeloxChem, Tutorial 1 to 4 (menu and window names).
- **Language reference:** copy of mdlib `docs/script_reference.md` (the command to refresh it is in Contributing-to-the-docs).
- **Examples:** `examples/` holds `showcase_gui.via`, two aspirin movie workspaces and `fov_and_focus.via` and `.xyz`.
- **Screenshots, taken from a `uinification` build with the aspirin showcase:** Quick-tour, System-window (both tabs), Attributes, Timeline (with the legend editor and the Properties menu), Distributions, Ramachandran (with the chip editor), Shape-space, Density-volume (with the layer editor), Representations, Animation (with the Operations menu), Script-editor, Rendering (Visuals menu), Spatial-view (tooltip and context menu).
- **Text checks:** every internal link and anchor resolves, keys are written with `<kbd>`, one `#` title per page.

## What is left

### 1. Screenshots

- **Movie maker pages** (7 `TODO screenshot` comments): needs a build of the `video` branch. The helper can do it, the Movie window has to be added to the layout.
- **Tutorials:** Tutorials 1, 2 and 3 are finished with new images. Tutorial 4 still has the 2023 images and needs data from the user. Scripts: `docs/ideas/tools/`.
- **Install, VeloxChem:** old images (Home is done).
- **Other states not yet captured:** a drag in progress. The Series right-click menu, Screenshot and Settings menus are done.
- **Seen while capturing:** in the Script Reference, the unit `Å` after the signature of `com` shows as a replacement character in the code block font of the Script Reference.
- **Check the images online** once pushed: relative paths `img/<page>/<name>.png` should render in the fork's wiki. If they do not, use `https://raw.githubusercontent.com/wiki/<owner>/viamd/img/...`.

### 2. Windows with no documentation yet

Done on day 2: **Script Reference** (page Script-reference), **Structure Export** and **Contacts**, with screenshots (taken from the GUI). Also new: the Settings page (units, were not documented), and the Screenshot menu image. Still only mentioned: Property Export (the Script Editor File menu offers Load, Save, Export). The debug window (<kbd>F11</kbd>) is only listed.

### 3. Things written from the source and not verified in the GUI

- Window and menu names in the rewritten window pages (the Windows menu says **Shapespace**, one word).
- Ramachandran: the contents of the **Layers** and **View** menus, the **Create Property** row and the **Reference** checkbox.
- Shape space: what the legend entries do (hide, highlight), whether points selected with the right mouse button can be saved as a group.
- `.tpr` files: verified on day 2 (`atom/charge` is in the Attribute list; the Contacts window reports LJ and Coulomb energies; the System window shows the force field line).
- Tutorial 2: `lj_sr_protein_ain` and `coul_sr_protein_ain` are confirmed (plotted from the edr file).
- Keyboard-and-mouse: the table was checked against the source, not by trying the keys.
- Troubleshooting: the rendering advice (turn off ambient occlusion, temporal effects).

### 4. Tutorials

- **Tutorial 1:** *Coloring by an attribute* is written with a `.tpr` (hMff peptide with SDS), the two `.tpr` examples are in `examples/` with their credits. The value filter on a Distributions plot (shading the Timeline) is dead code in all branches (`value_filter.enabled` is never set), so it is not documented.
- **Tutorial 3:** rewritten on day 2 around `distance_min(resname("PFT"), protein)` with two groups, `ads` and `free`; its workspace is `examples/tutorial-3.via` (built with the helper, not saved from the app). The amyloid evaluation takes 1.5 to 3 minutes per run. Images done: probe and group distances, dihedrals and planarity, shape space. SDF, overview done. Optional: the super_ads dihedral comparison image (needs another amyloid run).
- **Tutorial 2:** done on day 2: `examples/tutorial-2.via` (helper-built, loads and evaluates), `pocket_1` image taken from a Selection Query. The stored selection `pocket_2` is not in the file.
- **Tutorial 4:** the Workspace section is a sentence, there is no file.

### 5. Waiting for merges

- **`uinification`:** the pages for the series model, Create Property, Export entries and several layers carry "requires the unified series version" notes. Remove them after the merge.
- **mdlib:** the Language reference has no `ramachandran` or `shapespace` entries until the mdlib change is merged upstream. Then link `Language-reference#ramachandran` and `#shapespace` from the two pages again and refresh the file.
- **`video`:** the Movie maker pages need the `video` branch merged. The example links point to the `examples/` folder of the wiki and work now.

### 6. Publishing

1. Replace the owner in the raw links of Example-workspaces (`mathieulinares` to `scanberg`).
2. Decide who pushes: the `docu` branch has to become the `master` of `scanberg/viamd.wiki`, which needs write access to Robin's wiki.
3. The README of the viamd repository links to `wiki/0.-Building`; the stub keeps it working. Update the README to link `Install` when convenient.

### 7. Videos

Not before the wiki is finished. The tentative list and the replay idea are in `docs/ideas/tutorial-videos-and-session-replay.md`.

## Related work outside the wiki

- **mdlib and scanberg:** `master` of viamd and of mdlib are fast-forwarded to scanberg's. Your commits are in `uinification` (viamd, pushed) and in the mdlib branch `uinification` (fork, pushed).
- **`uinification-rebased` (mdlib, local only):** the three mdlib commits rebased on `upstream/master`, plus one WIP commit that ports the new procedures to the `md_bitset` API and the `EVAL_LOG_ERROR` macro. Not built, not tested, not pushed. To finish: build `md_unittest`, fix what breaks (the test `script.reference_documents_every_procedure` needs the new procedures documented), push to the fork, open a pull request on `scanberg/mdlib`, then point `ext/mdlib` of the viamd branch at it.

## Example data (day 2)

`examples/` of the wiki holds two `.tpr` files, shared under CC-BY 4.0 with their credit on Example-workspaces: the atomistic `hMff_60SDS_monomer_CHARMM36m_OPC_37C_replica0.tpr` (Morgan Regnier, Zenodo 10.5281/zenodo.7733867) and the Martini `M-4-900-300.tpr` (Matti Javanainen, Zenodo 10.5281/zenodo.3604289; the paper is "to be added" in the record, check it before publishing). Before publishing to Robin's wiki: owner in the raw links, and ask whether a 4.5 MB of binary files is welcome there.

## Lessons from the capture runs

- Do not run two VIAMD captures at once, and do not use the machine during a run (dictation tools type into the focused window).
- A Distributions plot opens at x 0 to 1 until it is fitted; on a long evaluation wait about a minute after it finishes, then double click the plot.
- A filtered distribution is not automatic: add the `(filtered)` entry from the Properties menu.
- Finish every long run with the guarded `cap` step (it checks that VIAMD is on top).

## The screenshot helper

`docs/ideas/tools/wiki_screenshots.py` launches a copy of the VIAMD binary, sets the window size, raises it, evaluates the script, clicks and grabs the window with Pillow. Lessons learned:

- Run it from a terminal that is not the VS Code snap terminal, or start VIAMD with `env -i` and a clean environment: the snap libraries break VIAMD.
- Run a **copy of the binary in a folder without `datasets/`**: the default alanine dataset otherwise finishes loading after the workspace and replaces it.
- The script evaluation of the showcase takes about 4 minutes (the `sdf` lines). Wait for it before capturing.
- It only touches the window of the process it started, found by its PID. Do not use it while using the machine: it moves the pointer and raises the window.
- Window layouts are written to an `imgui.ini` in the working folder, positions in window coordinates.
- Paths at the top of the file (`SRC`, `BIN`) must be adapted to the machine.

### Day 2, last run

The 2023 images are all replaced (Property Export, File menu, Home overview, coordinate widget, Script suggestions menu); only the logos and the Videos and Tutorial 4 images remain. Link, anchor and image check passes (the `flatten` anchor is an explicit `<a id>`). Scripts: `t15a` (Property Export), `t15b` (widget, File menu), `t15d` (shift+click and context menu, with a patched click), `t15e` (pocket selection), `t15f` (tutorial-2.via).

### Bond orders

Documented on the Representations page from the source: orders come only from the VeloxChem reader (`md_vlx.c`); `compute_covalent_bond_order` in mdlib is not called, so inferred bonds are all single. No image: the local build has `MD_ENABLE_HDF5=OFF` (and no HDF5 headers), so it cannot open the `.h5` files in `~/Downloads`. To take it: build with HDF5, load `biphenyl-scf-*.h5`, Ball And Stick with bond orders on and off (`docs/ideas/tools/t16a.py` has the workspace).

### Tutorial 4 (in progress, day 2 evening)

Data: `~/Desktop/bioexcel/tutorial-4/` (gro 222k atoms, `ef.xtc` 1.65 GB, about 1990 frames at 100 ps). Done: images `loaded`, `sodium-positions`, `densities` in `img/Tutorial-4-Sodium-through-a-membrane/` (scripts `t17a`, `t17b`, `t17d`, `t17e`); the page text is not rewritten yet.
Facts: ion 103 (`resname("SOD")[103]`) sits in the membrane from about 65 to 105 ns, use frame 1000. A radial grow of 5 A around it reaches only CYS508 and PHE785 (the grow is limited to the visible atoms, hence "hide all but the protein"). Operations in the workspace: `ApplyPbc=1`, `UnwrapStructures=1`; no `[Camera]`. F2 on a selection centers and gives a side view. The keyboard is Swedish: brackets need AltGr (helper patched).
Left: images of the ion in the pore, the grow (`t17f.py` gets the Grow window at 5.00, check the last capture), the `bn` representation and the count `c` in a second Timeline subplot; define `bn = residue({508,785})`; rewrite the page text (8 old images, `water()` and `protein()` in the script); build `examples/tutorial-4.via` and link it; then push.
