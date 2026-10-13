#!/usr/bin/env python3
"""Validate and replay a JSON GUI scenario with the local X11 capture helper."""

import argparse
import json
import os
import re
import sys


def fail(message):
    raise ValueError(message)


def number(value, label, minimum=None):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"{label} must be a number")
    if minimum is not None and value < minimum:
        fail(f"{label} must be at least {minimum}")
    return value


def text(value, label):
    if not isinstance(value, str) or not value:
        fail(f"{label} must be a non-empty string")
    return value


def compile_steps(steps):
    if not isinstance(steps, list) or not steps:
        fail("steps must be a non-empty list")
    compiled = []
    for index, step in enumerate(steps, 1):
        label = f"steps[{index - 1}]"
        if not isinstance(step, dict):
            fail(f"{label} must be an object")
        action = text(step.get("action"), f"{label}.action")
        caption = step.get("caption")
        if caption is not None and not isinstance(caption, str):
            fail(f"{label}.caption must be text")

        if action == "wait":
            item = ("wait", number(step.get("seconds"), f"{label}.seconds", 0))
        elif action in ("click", "right_click", "double_click", "move"):
            x = number(step.get("x"), f"{label}.x")
            y = number(step.get("y"), f"{label}.y")
            kind = {"right_click": "rclick", "double_click": "dbl"}.get(action, action)
            item = (kind, x, y)
        elif action == "drag":
            x0 = number(step.get("x0"), f"{label}.x0")
            y0 = number(step.get("y0"), f"{label}.y0")
            x1 = number(step.get("x1"), f"{label}.x1")
            y1 = number(step.get("y1"), f"{label}.y1")
            duration = number(step.get("seconds", 0.5), f"{label}.seconds", 0)
            samples = step.get("samples", 20)
            button = step.get("button", 1)
            if not isinstance(samples, int) or isinstance(samples, bool) or samples < 1:
                fail(f"{label}.samples must be a positive integer")
            if button not in (1, 2, 3):
                fail(f"{label}.button must be 1, 2 or 3")
            item = ("drag", x0, y0, x1, y1, duration, samples, button)
        elif action == "scroll":
            x = number(step.get("x"), f"{label}.x")
            y = number(step.get("y"), f"{label}.y")
            direction = step.get("direction")
            if direction not in ("up", "down"):
                fail(f"{label}.direction must be 'up' or 'down'")
            count = step.get("count", 1)
            if not isinstance(count, int) or isinstance(count, bool) or count < 1:
                fail(f"{label}.count must be a positive integer")
            item = ("scroll", x, y, direction, count)
        elif action == "key":
            key_name = text(step.get("name"), f"{label}.name")
            shift = step.get("shift", False)
            altgr = step.get("altgr", False)
            if not isinstance(shift, bool) or not isinstance(altgr, bool):
                fail(f"{label} shift and altgr must be booleans")
            item = ("key", key_name, shift, altgr)
        elif action == "type":
            value = step.get("text")
            if not isinstance(value, str):
                fail(f"{label}.text must be text")
            item = ("type", value)
        elif action == "evaluate":
            item = ("eval", number(step.get("wait_seconds"), f"{label}.wait_seconds", 0))
        elif action == "caption":
            item = ("caption", text(step.get("text"), f"{label}.text"))
        elif action == "record_video":
            name = text(step.get("name"), f"{label}.name")
            if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
                fail(f"{label}.name may contain only letters, numbers, '_' and '-'")
            duration = number(step.get("seconds"), f"{label}.seconds", 0.1)
            if duration > 900:
                fail(f"{label}.seconds must not exceed 900")
            fps = step.get("fps", 15)
            if not isinstance(fps, int) or isinstance(fps, bool) or not 1 <= fps <= 30:
                fail(f"{label}.fps must be an integer from 1 to 30")
            subtitle = step.get("subtitle", "")
            if not isinstance(subtitle, str):
                fail(f"{label}.subtitle must be text")
            overlay = step.get("overlay", True)
            if not isinstance(overlay, bool):
                fail(f"{label}.overlay must be a boolean")
            bitrate = step.get("bitrate_kbps", 6000)
            if not isinstance(bitrate, int) or isinstance(bitrate, bool) or not 100 <= bitrate <= 25000:
                fail(f"{label}.bitrate_kbps must be an integer from 100 to 25000")
            temp_dir = step.get("temp_dir")
            if temp_dir is not None:
                text(temp_dir, f"{label}.temp_dir")
            crop = step.get("crop")
            if crop is not None:
                if not isinstance(crop, list) or len(crop) != 4 or any(not isinstance(v, int) for v in crop):
                    fail(f"{label}.crop must be four integer window coordinates or null")
            output_size = step.get("output_size")
            if output_size is not None:
                if not isinstance(output_size, list) or len(output_size) != 2 or any(not isinstance(v, int) or isinstance(v, bool) for v in output_size):
                    fail(f"{label}.output_size must be two integers")
                if any(v <= 0 or v % 2 for v in output_size):
                    fail(f"{label}.output_size dimensions must be positive even numbers")
            raw_events = step.get("events", [])
            if not isinstance(raw_events, list):
                fail(f"{label}.events must be a list")
            events = []
            last_at = -1.0
            for event_index, event in enumerate(raw_events):
                event_label = f"{label}.events[{event_index}]"
                if not isinstance(event, dict):
                    fail(f"{event_label} must be an object")
                at = number(event.get("at"), f"{event_label}.at", 0)
                if at < last_at or at >= duration:
                    fail(f"{event_label}.at must be ordered and less than the recording duration")
                last_at = at
                event_step = dict(event)
                del event_step["at"]
                compiled_event, _ = compile_steps([event_step])[0]
                if compiled_event[0] not in ("click", "rclick", "dbl", "move", "drag", "scroll", "key", "type", "eval", "away", "caption"):
                    fail(f"{event_label} is not a supported timed recording action")
                events.append((at, compiled_event))
            item = ("record", name, duration, fps, crop, subtitle, events, output_size, overlay, bitrate, temp_dir)
        elif action == "capture":
            name = text(step.get("name"), f"{label}.name")
            if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
                fail(f"{label}.name may contain only letters, numbers, '_' and '-' ")
            crop = step.get("crop")
            if crop is not None:
                if not isinstance(crop, list) or len(crop) != 4 or any(not isinstance(v, int) for v in crop):
                    fail(f"{label}.crop must be four integer window coordinates or null")
            item = ("cap", name, crop)
        elif action == "away":
            item = ("away",)
        else:
            fail(f"{label} has unsupported action {action!r}")
        compiled.append((item, caption))
    return compiled


def load_scenario(path):
    with open(path, encoding="utf-8") as handle:
        scenario = json.load(handle)
    if not isinstance(scenario, dict):
        fail("scenario root must be an object")

    title = text(scenario.get("title"), "title")
    workspace = text(scenario.get("workspace"), "workspace")
    if os.path.basename(workspace) != workspace:
        fail("workspace must be a filename in the source directory")
    settle = number(scenario.get("settle_seconds", 20), "settle_seconds", 0)
    workspace_file = scenario.get("workspace_file")
    if workspace_file is not None:
        text(workspace_file, "workspace_file")

    show = scenario.get("show", [])
    if not isinstance(show, list) or any(not isinstance(name, str) for name in show):
        fail("show must be a list of window names")
    layout = scenario.get("layout", {})
    if not isinstance(layout, dict):
        fail("layout must map window names to [x, y, width, height]")
    for name, rect in layout.items():
        if not isinstance(name, str) or not isinstance(rect, list) or len(rect) != 4:
            fail("each layout entry must be a window name and a four-number rectangle")
        for value in rect:
            number(value, f"layout[{name!r}]")

    window = scenario.get("window", {})
    if not isinstance(window, dict):
        fail("window must be an object")
    for key in ("size", "position"):
        if key in window:
            pair = window[key]
            if not isinstance(pair, list) or len(pair) != 2 or any(not isinstance(v, int) for v in pair):
                fail(f"window.{key} must be two integers")
    if "size" in window and any(value <= 0 for value in window["size"]):
        fail("window.size values must be positive")
    if "script_editor_rect" in scenario:
        rect = scenario["script_editor_rect"]
        if not isinstance(rect, list) or len(rect) != 4:
            fail("script_editor_rect must be [x, y, width, height]")
        for value in rect:
            number(value, "script_editor_rect")

    compiled = compile_steps(scenario.get("steps"))
    return scenario, title, workspace, settle, show, layout, window, compiled


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", help="JSON scenario file")
    parser.add_argument("--dry-run", action="store_true", help="validate and print steps without controlling the GUI")
    parser.add_argument("--preview", action="store_true", help="keep VIAMD open after replay so you can inspect it; press Enter here to close")
    parser.add_argument("--source-dir", help="directory containing the workspace and its input files")
    parser.add_argument("--binary", help="VIAMD executable (defaults to the screenshot helper setting)")
    parser.add_argument("--output-dir", help="directory for PNG and MP4 outputs (defaults to scenario folder/captures)")
    args = parser.parse_args()

    try:
        scenario, title, workspace, settle, show, layout, window, compiled = load_scenario(args.scenario)
        source_dir = os.path.abspath(os.path.expanduser(args.source_dir or scenario.get("source_dir", "~/Desktop/bioexcel/aspirin")))
        workspace_file = scenario.get("workspace_file")
        workspace_path = os.path.abspath(os.path.expanduser(workspace_file)) if workspace_file else os.path.join(source_dir, workspace)
        if not os.path.isfile(workspace_path):
            fail(f"workspace not found: {workspace_path}")

        print(f"Scenario: {title}")
        print(f"Workspace: {workspace_path}")
        print(f"Settle: {settle:g} s; visible windows: {', '.join(show) or '(workspace defaults)'}")
        for index, (item, caption) in enumerate(compiled, 1):
            print(f"{index:02d}. {item[0]} {item[1:]}" + (f"  [{caption}]" if caption else ""))
        if args.dry_run:
            return 0

        import shot

        shot.SRC = source_dir
        binary = args.binary or scenario.get("binary")
        if binary:
            shot.BIN = os.path.abspath(os.path.expanduser(binary))
        output_dir = args.output_dir or scenario.get("output_dir")
        if output_dir:
            output_dir = os.path.expanduser(output_dir)
            if not os.path.isabs(output_dir):
                output_dir = os.path.join(os.path.dirname(os.path.abspath(args.scenario)), output_dir)
            shot.OUT = os.path.abspath(output_dir)
        else:
            shot.OUT = os.path.join(os.path.dirname(os.path.abspath(args.scenario)), "captures")
        if "size" in window:
            shot.W, shot.H = window["size"]
        if "position" in window:
            shot.X0, shot.Y0 = window["position"]
        if "script_editor_rect" in scenario:
            shot.SE = tuple(scenario["script_editor_rect"])
        if shot.dpy is None:
            fail("cannot open the X11 display; GUI replay requires a local X11 session")

        frames = shot.session(workspace, show, {name: tuple(rect) for name, rect in layout.items()}, [item for item, _ in compiled], settle, keep_open=args.preview, workspace_file=workspace_path)
        print(f"Saved {len(frames)} output(s) in {shot.OUT}")
        return 0
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"scenario error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())