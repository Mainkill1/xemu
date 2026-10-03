#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep the hard-FPU FNSTSW AX path generated and checkpointed."""

from pathlib import Path
import re
import unittest


TRANSLATE = Path(__file__).resolve().parents[2] / "target/i386/tcg/translate.c"


class FnstswGenerationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = TRANSLATE.read_text(encoding="utf-8")

    def test_ax_dispatch_uses_generated_path(self):
        dispatch = re.search(
            r"case 0x3c: /\* df/4 \*/(?P<body>.*?)case 0x3d:",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(dispatch)
        self.assertIn("gen_fnstsw_ax(s, s->tmp2_i32);", dispatch.group("body"))
        self.assertNotIn("gen_helper_fnstsw", dispatch.group("body"))

    def test_generated_path_keeps_soft_fallback_and_fp_checkpoint(self):
        function = re.search(
            r"static void gen_fnstsw_ax\(.*?\n\}(?=\n\nstatic)",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(function)
        body = function.group(0)
        self.assertIn("GEN_HELPER_FALLBACK_T_v(fnstsw, result);", body)
        self.assertLess(body.index("gen_flush_fp(s);"), body.index("fpus"))
        self.assertIn("offsetof(CPUX86State, fpus)", body)
        self.assertIn("tcg_gen_andi_i32(result, result, ~0x3800);", body)
        self.assertIn("tcg_gen_andi_i32(top, fpstt, 7);", body)
        self.assertIn("tcg_gen_shli_i32(top, top, 11);", body)


if __name__ == "__main__":
    unittest.main()
