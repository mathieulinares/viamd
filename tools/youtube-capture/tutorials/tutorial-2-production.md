# Tutorial 2 Movie Production Notes

## Deliverables

- Clean GUI capture: `~/Videos/VIAMD/tutorial-2-aspirin-phospholipase.mp4`
- Time-coded spoken narration: [tutorial-2-narration.md](tutorial-2-narration.md)
- Replay scenario: [tutorial-2-full.json](../scenarios/tutorial-2-full.json)
- Source workspace: `~/viamd.wiki/examples/tutorial-2.via`

The base MP4 is silent. The scenario deliberately disables the cursor and caption overlay; record or add the spoken narration as a separate voice track against the timecodes in the script. No words are burned into the picture.

## Source Files

Keep these files together in `~/Desktop/bioexcel/aspirin/`:

- `aspirin-phospholipase.gro`
- `aspirin-phospholipase.xtc`
- `aspirin-phospholipase.edr`

The scenario reads the wiki workspace separately and stages a temporary copy beside the linked data files. It does not modify the wiki workspace or the original simulation files. The XTC is large; do not copy it into the VIAMD repository.

## Recording

From the VIAMD repository root, validate the scenario without controlling the desktop:

```bash
python3 -B tools/youtube-capture/replay_scenario.py tools/youtube-capture/scenarios/tutorial-2-full.json --dry-run
```

Record the clean GUI track:

```bash
python3 tools/youtube-capture/replay_scenario.py tools/youtube-capture/scenarios/tutorial-2-full.json
```

The replay controls the desktop while VIAMD loads the 108 MB trajectory, evaluates the workspace script, zooms into the complex, and records for ten minutes. Do not use the mouse or keyboard until it exits. The current output is H.264, 1280x720, 10 fps at 2.2 Mbps; temporary frames are written to `/dev/shm` to avoid filling the nearly full home filesystem. The scene uses the supplied finished workspace so the representations, properties, and plots are available; the narration explains how to build that state from a fresh session. The capture includes camera rotation and several trajectory-playback intervals, but it does not automate every menu operation described in the voiceover.

**Review status:** The current MP4 has a visible aspirin complex and populated Timeline, but both Distribution panels are empty and the first frame contains a native atom tooltip. A separate test showed a double-click can fit one histogram, but fitting both subplots together did not persist. Treat this as a review draft, not the final publishable cut; fit the plots, move the pointer to a neutral area before recording, then rerender and inspect those sections before publishing.

## Session Handoff (2026-10-13)

- The workspace and 50,515-atom GRO, 601-frame XTC, and EDR were verified in VIAMD's System window.
- VIAMD's startup queue loads its built-in 1ALA dataset after queued workspace files. The replay helper therefore starts VIAMD without arguments and opens the staged `.via` through File > Open Workspace.
- The X11 keymap maps `/` to `Shift+7`; absolute paths typed into the GTK picker must use that mapping.
- The first recording attempt used the wrong dataset. A later high-bitrate attempt was stopped to protect the nearly full home filesystem. The final draft uses `/dev/shm` for JPEG frames, 2.2 Mbps H.264 and completed at 148 MB.
- The current scenario loads the finished workspace, evaluates its script, zooms the viewport, then records ten minutes. It is a narrated walkthrough of the concepts, not a GUI-action-for-action tutorial recording.

## Voice Track

Read the narration at a conversational pace, aiming for roughly 90-120 words per minute. The time ranges are fixed video timecodes, not estimated chapter lengths. Leave the marked visual space at the end of each beat. Record the voice track separately so it can be retaken without regenerating the GUI capture, then align it to the chapter boundaries and export a final muxed copy. Keep the silent source MP4 unchanged.

Export the narration as uncompressed WAV at 48 kHz, mono or stereo, and align it to the exact 10-minute picture duration in an audio editor. Keep the narration track silent where the script calls for visual breathing room. Save it as `~/Videos/VIAMD/tutorial-2-voice.wav`. The following tested GStreamer pipeline copies the H.264 picture without re-encoding and adds AAC audio:

```bash
gst-launch-1.0 -e \
	mp4mux name=mux faststart=true ! filesink location="$HOME/Videos/VIAMD/tutorial-2-final.mp4" \
	filesrc location="$HOME/Videos/VIAMD/tutorial-2-aspirin-phospholipase.mp4" ! qtdemux name=video \
	video.video_0 ! queue ! mux.video_0 \
	filesrc location="$HOME/Videos/VIAMD/tutorial-2-voice.wav" ! wavparse ! audioconvert ! audioresample \
		! avenc_aac bitrate=192000 ! aacparse ! mux.audio_0
```

Before publishing, verify the output duration is 10:00, the plots and ligand remain visible at the stated beats, the voice track ends within the picture, and the video contains no burned-in captions or cursor graphic.