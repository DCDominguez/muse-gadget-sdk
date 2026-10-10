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

"""bulletin.read: advertised and answered under the same option, built from
a notes file that's embedded whole, and kept small enough to send."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LinkBulletinTest(unittest.TestCase):
    def test_command_is_advertised_and_dispatched_with_the_feature(self):
        noise = (ROOT / "main/noise_control.cpp").read_text()
        start = noise.index("#if CONFIG_HOMEHUB_BULLETIN")
        self.assertIn('add_command(commands, "bulletin.read"', noise[start:noise.index("#endif", start)])
        app = (ROOT / "main/app.c").read_text()
        start = app.index("static cJSON *on_ws_command(")
        dispatch = app[start:app.index("unsupported command", start)]
        block = dispatch[dispatch.index("#if CONFIG_HOMEHUB_BULLETIN"):]
        for name in ('"bulletin.read"', '"bulletin.post"'):
            self.assertIn(name, block[:block.index("#endif")])
        start = noise.index("#if CONFIG_HOMEHUB_BULLETIN")
        self.assertIn('add_command(commands, "bulletin.post"', noise[start:noise.index("#endif", start)])

    def test_ideas_are_checked_and_bounded(self):
        source = (ROOT / "main/bulletin.c").read_text()
        post = source[source.index("cJSON *bulletin_post_command("):]
        self.assertIn('"idea"', post)
        self.assertIn("BULLETIN_IDEA_MAX", post)
        self.assertIn("IDEAS_MAX", post)
        self.assertIn("IDEAS_BYTES", source[source.index("static esp_err_t save_ideas("):])

    def test_the_default_file_is_embedded_under_a_fixed_name(self):
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text()
        default = re.search(r'config HOMEHUB_BULLETIN_FILE.*?default "([^"]+)"', kconfig, re.S).group(1)
        notes = (ROOT / "main" / default).resolve()
        self.assertTrue(notes.is_file(), notes)
        cmake = (ROOT / "main/CMakeLists.txt").read_text()
        self.assertIn('"${CMAKE_CURRENT_BINARY_DIR}/bulletin.md"', cmake)
        self.assertIn("EMBED_TXTFILES ${GADGET_EMBED}", cmake)
        self.assertIn('asm("_binary_bulletin_md_start")', (ROOT / "main/bulletin.c").read_text())

    def test_the_notes_stay_small_and_free_of_secrets(self):
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text()
        default = re.search(r'config HOMEHUB_BULLETIN_FILE.*?default "([^"]+)"', kconfig, re.S).group(1)
        text = (ROOT / "main" / default).read_text(encoding="utf-8")
        # A command result goes in one Noise message; leave plenty of room.
        self.assertLess(len(text.encode()), 8000)
        self.assertNotRegex(text, r"mgst_|sk-[A-Za-z0-9]|\b\d{1,3}(\.\d{1,3}){3}\b")


if __name__ == "__main__":
    unittest.main()
