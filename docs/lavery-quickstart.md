# Lavery parameters: quick start

The Lavery window computes Curves+/3DNA-style base-pair parameters for
nucleic-acid trajectories:

- six **intra-base-pair** parameters: Shear, Stretch, Stagger, Buckle, Propeller, Opening;
- six **inter-base-pair (step)** parameters: Shift, Slide, Rise, Tilt, Roll, Twist.

It then flags frames where a pair or step deviates from its own trajectory
baseline. For the full reference, see [lavery.md](./lavery.md).

## Five-step workflow

1. Load a nucleic-acid structure or trajectory and open **Lavery** from the
   Windows menu. The analysis starts automatically.
   Settings sit in a sidebar on the left; the events table is on top, with
   the overview (count plot, heatmap) and inspector (timeline, distribution)
   below. The timeline and distribution refit when you select a new series.
2. Choose a class, parameter and pair or step. The time series, heatmap and
   inspector all follow your selection.
3. Set **Outlier sigma** and a **Baseline**:
   - **Mean/SD** (default);
   - **Median/MAD**, which stays robust to large excursions.
4. Open **Outlier events**. Each row is one local distortion: outliers that
   are close in time and along the helix, so separate kinks are listed
   separately. Rows are sorted by **Score**, and each shows its base-pair
   region and a **Type** such as Unwinding, Kink (roll) or Pair loss.
   - Click a row to jump to its peak and inspect only that event. Resume playback
     or change frame to restore all active events. No pin controls or viewport box.
   - Expand a row to list the pairs and steps involved.
   - Click a pair or step to select it at its peak frame.
   - Click one of its parameter buttons (name and peak z) to select that
     parameter at the frame where it peaks.
   - **Select** puts the event's nucleotides in the VIAMD selection.
   - **Timeline** adds the event as a period on the main timeline.
   - **Hierarchical events** (on by default) keeps broad parents and adds
     stronger localized children labelled **child of #N**. **Burst extra**
     defaults to +1 sigma: at tolerance 2.5, children start at 3.5 sigma.
     Increase it if broad activity still merges the local burst.

   All plots update with each click.
   The default **Min frames = 3** filters single-frame spikes for parents and
   children. Set 1 when investigating spikes; saved workspace values are retained.
   **Frames / span** shows contributing samples separately from elapsed time:
   one sample is "1 contributing frame", even though its elapsed time is zero.
5. Look at the viewport:
   - **Rings** on both bases mark an outlying base pair.
   - A **bar** between two pair centres marks an outlying step.
   - Crimson means above the baseline, cyan below, grey means previewed but not
     outlying.
   - Hover a ring or bar to see all six parameters with their values,
     z-scores and outlier flags.
   - Click a ring or bar to select that member.
   - Play the trajectory: all active events automatically get a broad coloured
     halo over their overall region. Rings and bars show only current-frame
     contributions (including hysteresis); amber rings mark lost pairs.
     Glow colours match event-table swatches, count-plot bands and newly added
     timeline periods. Events overlapping in time get different colour slots;
     colours stay stable during playback and table sorting.
   - Shared regions blend event colours without stacking brightness.
     Local children have a stronger colour weight inside their parent's halo.
     Hover the glow, markers or involved atoms for
     event IDs, types, contributing parameters, values and z-scores.
     No focus mode, pinned event, or permanent viewport text box.
   - The GUI is one fixed-layout window: settings left, events above the plots.
     Sections scroll independently without dock tabs or panel title bars.

## Useful controls

| Control | Effect |
|---|---|
| Detect on | Event parameters: Intra / Inter / Both presets (default Both) or any of the 12 via the checkbox grid; independent of the plotted class |
| Neighbourhood (bp) | Outliers join one event only if at most this many base pairs apart (default 2) |
| Whole structure | Ignore position: one event per stretch of outlying frames |
| Hierarchical events | Keep broad parents plus higher-threshold localized children (default on); off restores flat grouping |
| Burst extra | Child start = tolerance + this (default +1 sigma); children retain the same hysteresis and duration/gap filters |
| Hysteresis | Events start at the tolerance and continue down to tolerance minus this (default 0.5 sigma) |
| Pair loss | A broken base pair counts as outlying instead of ending the event (default on) |
| Min frames | Require N contributing frames (default 3); bridged gaps do not count; 1 retains spikes |
| Bridge gaps | Merge outliers separated by up to N quiet frames |
| All to timeline / Remove from timeline | Add or remove every event as a period on the main timeline |
| Export events CSV | One row per event, member and parameter |
| Export CSV | Per-frame parameter values, z-scores and flags (ends with a `baseline` column) |

The outlier-count plot shades events and draws a dashed line at the number of
outliers expected by chance. If the curve stays near that line, most hits are
noise.

The hierarchy is two-level and intensity-based. Children must touch fewer base
pairs than their parent; equal-strength activity may still need a higher Burst
extra setting to separate. Parent/child scores overlap: do not add them as
independent events. CSV includes `parent_event` and `burst_sigma`.

Angular parameters such as Twist and Opening wrap correctly at +/-180°.

## What's left to do

### Validation still needed

- [ ] GUI check of playback and scrubbing: every active event should show its
  coloured extent and current contributors, including overlapping parents and
  localized children. Check hover details and pair-loss markers.
- [ ] GUI check of paused inspection: clicking an event, member or parameter
  should hide other events; Play or a frame change should restore automatic
  feedback without leaving stale highlights.
- [ ] GUI check of the fixed layout at smaller window sizes, plot refitting,
  saved workspace settings, timeline actions and CSV export.
- [ ] Validate event thresholds and hierarchy on representative trajectories,
  not just synthetic cases. Confirm that detected bursts reflect meaningful
  structural changes rather than noise or pairing-quality failures.

### Future improvements (not implemented)

- [ ] A minimum duration in physical time (ps/ns), alongside the existing
  contributing-frame minimum, for trajectories with different sampling rates.
- [ ] Local-background or multi-scale event separation: the current hierarchy
  has only two intensity levels and can miss local activity that is not stronger
  than the broad background.
- [ ] Performance profiling on long trajectories and visual tuning for many
  simultaneous events; large overlaps can exhaust easily distinguishable colours.

Automated validation currently covers 61 Lavery tests. This does not replace
the GUI and scientific checks above.
