#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Headless smoke and deterministic framebuffer tests for the UI simulator."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

WIDTH = HEIGHT = 412
TAB5_WIDTH, TAB5_HEIGHT = 1280, 720
HERE = Path(__file__).resolve().parent
SCENARIOS = tuple(sorted((HERE / "scenarios").glob("*.txt")))
# Run with --board tab5: an 800x480 Muse with muse_dock around it.
TAB5_SCENARIOS = tuple(sorted((HERE / "scenarios_tab5").glob("*.txt")))


def read_ppm(path: Path, width: int = WIDTH, height: int = HEIGHT) -> bytes:
    raw = path.read_bytes()
    header = f"P6\n{width} {height}\n255\n".encode()
    assert raw.startswith(header), f"{path}: wrong PPM header"
    pixels = raw[len(header) :]
    assert len(pixels) == width * height * 3, f"{path}: truncated framebuffer"
    assert len(set(pixels)) > 8, f"{path}: framebuffer has too few colours"
    return pixels


def render(
    binary: Path, scenario: Path, output: Path, board: str = "watcher"
) -> tuple[str, subprocess.CompletedProcess[str]]:
    env = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
    proc = subprocess.run(
        [
            str(binary),
            "--board",
            board,
            "--headless",
            "--scenario",
            str(scenario),
            "--run-ms",
            "200",
            "--screenshot",
            str(output),
        ],
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=30,
    )
    assert proc.returncode == 0, f"{scenario.name}:\n{proc.stdout}\n{proc.stderr}"
    pixels = read_ppm(output, *((TAB5_WIDTH, TAB5_HEIGHT) if board == "tab5" else (WIDTH, HEIGHT)))
    return hashlib.sha256(pixels).hexdigest(), proc


def region(path: Path, x0: int, y0: int, x1: int, y1: int) -> bytes:
    """Pixels of a Tab5 screenshot inside [x0, x1) x [y0, y1)."""
    pixels = read_ppm(path, TAB5_WIDTH, TAB5_HEIGHT)
    row = TAB5_WIDTH * 3
    return b"".join(pixels[y * row + x0 * 3 : y * row + x1 * 3] for y in range(y0, y1))


def check_tab5(binary: Path, tmp_path: Path) -> dict[str, str]:
    assert TAB5_SCENARIOS, "no Tab5 simulator scenarios found"
    hashes: dict[str, str] = {}
    shots: dict[str, Path] = {}
    for scenario in TAB5_SCENARIOS:
        shot = tmp_path / f"tab5-{scenario.stem}-1.ppm"
        first, _ = render(binary, scenario, shot, "tab5")
        second, _ = render(binary, scenario, tmp_path / f"tab5-{scenario.stem}-2.ppm", "tab5")
        assert first == second, f"tab5 {scenario.name}: framebuffer is not deterministic"
        hashes[f"tab5-{scenario.stem}"] = first
        shots[scenario.stem] = shot

    # Tapping the chat line shows the on-screen keyboard over the strip below
    # Muse (it once opened off the bottom of the screen).
    strip = (0, 492, 800, 720)
    assert region(shots["idle"], *strip) != region(shots["osk"], *strip), "chat tap didn't show the keyboard"
    # Typed text and its reply land in the chat log.
    log = (812, 210, 1268, 640)
    assert region(shots["idle"], *log) != region(shots["chat"], *log), "typed chat didn't reach the log"
    return hashes


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    assert binary.is_file(), binary
    assert SCENARIOS, "no simulator scenarios found"

    with tempfile.TemporaryDirectory(prefix="muse-simulator-test-") as tmp:
        tmp_path = Path(tmp)
        hashes: dict[str, str] = {}
        for scenario in SCENARIOS:
            first, _ = render(binary, scenario, tmp_path / f"{scenario.stem}-1.ppm")
            second, _ = render(binary, scenario, tmp_path / f"{scenario.stem}-2.ppm")
            assert first == second, f"{scenario.name}: framebuffer is not deterministic"
            hashes[scenario.stem] = first

        assert len(set(hashes.values())) == len(hashes), f"scenarios rendered identically: {hashes}"
        hashes.update(check_tab5(binary, tmp_path))

        # Showing shutdown must not lock subsequent preview state selections.
        after_off = tmp_path / "after-off.txt"
        after_off.write_text("face=off\n" + (HERE / "scenarios/listening.txt").read_text())
        recovered, _ = render(binary, after_off, tmp_path / "after-off.ppm")
        assert recovered == hashes["listening"], "Off prevented the next preview state"

        invalid = tmp_path / "invalid.txt"
        for setting in ("face=definitely-not-a-mode", "level=nan"):
            invalid.write_text(f"{setting}\n")
            proc = subprocess.run(
                [str(binary), "--headless", "--scenario", str(invalid)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=10,
            )
            assert proc.returncode == 2
            assert "unsupported or invalid setting" in proc.stderr

    for name, digest in sorted(hashes.items()):
        print(f"{name}: {digest}")


if __name__ == "__main__":
    main()
