"""ToonNet's tests: python3 -m unittest test_toon_model (from tools/toon_model)."""

from __future__ import annotations

import json
import random
import tempfile
import unittest
from pathlib import Path

import numpy as np
import torch

import cond
from model import PatchCritic, ToonNet, count


class CondTest(unittest.TestCase):
    def test_the_example_matches_the_app(self) -> None:
        # Kotlin's ToonEngineTest checks the same vector.
        self.assertEqual(cond.COND, 31)
        got = cond.cond_from_params(cond.EXAMPLE_PARAMS, True)
        self.assertEqual(len(got), cond.COND)
        np.testing.assert_allclose(got, cond.EXAMPLE_COND, atol=1e-6)

    def test_out_of_range_is_clamped_and_wrong_sizes_refused(self) -> None:
        wild = cond.cond_from_params([99.0] * cond.PARAMS, False)
        self.assertTrue(all(0.0 <= v <= 1.0 for v in wild))
        self.assertEqual((wild[5], wild[11], wild[15], wild[-1]), (1.0, 1.0, 1.0, 0.0))
        with self.assertRaises(ValueError):
            cond.cond_from_params([0.0] * 3, True)

    def test_style_starts_match_the_app_catalogue(self) -> None:
        catalog = Path(__file__).resolve().parents[2] / "src/services/toon/catalog.ts"
        text = catalog.read_text()
        for st in cond.STYLE_START:
            lines, shades, vivid, detail, eyes, skin, glow = st
            want = f"start: {{ lines: {lines:g}, shades: {shades:g}, vivid: {vivid:g}, detail: {detail:g}, eyes: {eyes:g}, skin: {skin:g}, glow: {glow:g} }}"
            self.assertIn(want, text)

    def test_sampled_settings_stay_in_the_apps_ranges(self) -> None:
        rng = random.Random(3)
        for _ in range(500):
            p = cond.sample_params(rng)
            self.assertEqual(len(p), cond.PARAMS)
            self.assertTrue(0 <= p[0] <= 5 and 0 <= p[1] <= 5 and 0 <= p[17] <= 3)
            self.assertIn(p[3], (2, 3, 4, 5, 6))
            for i, lo, hi in cond.SLIDERS:
                self.assertTrue(lo <= p[i] <= hi, (i, p[i]))


class ModelTest(unittest.TestCase):
    def setUp(self) -> None:
        torch.manual_seed(0)
        self.net = ToonNet().eval()

    def test_shapes_range_and_size(self) -> None:
        x = torch.rand(2, 3, 64, 96) * 2 - 1
        out = self.net(x, torch.rand(2, 1, 64, 96), torch.rand(2, cond.COND))
        self.assertEqual(out.shape, (2, 3, 64, 96))
        self.assertTrue(bool((out.abs() <= 1).all()))
        self.assertLess(count(self.net), 2_000_000)  # phone-sized
        self.assertGreater(count(PatchCritic()), 0)

    def test_the_settings_steer_the_picture(self) -> None:
        # Untrained, FiLM and each block's last layer start at zero (stable training);
        # give them weights and the settings must change the result.
        with torch.no_grad():
            for p in self.net.film.parameters():
                p.normal_(0, 0.5)
            for blk in self.net.body:
                blk.pw2.weight.normal_(0, 0.05)
        x, m = torch.rand(1, 3, 64, 64) * 2 - 1, torch.rand(1, 1, 64, 64)
        a = self.net(x, m, torch.tensor([cond.cond_from_params(cond.start_params(0), True)]))
        b = self.net(x, m, torch.tensor([cond.cond_from_params(cond.start_params(3), True)]))
        self.assertGreater(float((a - b).abs().mean()), 1e-3)

    def test_fully_convolutional_any_multiple_of_eight(self) -> None:
        for h, w in ((32, 32), (48, 80), (128, 72)):
            out = self.net(torch.zeros(1, 3, h, w), torch.zeros(1, 1, h, w), torch.zeros(1, cond.COND))
            self.assertEqual(out.shape[-2:], (h, w))


class TrainingTest(unittest.TestCase):
    def test_losses_and_one_step_of_each_stage(self) -> None:
        import train

        net, critic = ToonNet(16, 2), PatchCritic(8)
        opt = torch.optim.AdamW(net.parameters(), 1e-3)
        x, m, c = torch.rand(2, 3, 64, 64) * 2 - 1, torch.rand(2, 1, 64, 64), torch.rand(2, cond.COND)
        tgt = torch.rand(2, 3, 64, 64) * 2 - 1
        loss, parts = train.distil_loss(net(x, m, c), tgt)
        self.assertTrue(torch.isfinite(loss) and parts["l1"] > 0)
        zero, _ = train.distil_loss(tgt, tgt)
        self.assertAlmostEqual(float(zero), 0.0, places=6)
        before = [p.clone() for p in net.parameters()]
        opt.zero_grad()
        loss.backward()
        opt.step()
        self.assertTrue(any(not torch.equal(a, b) for a, b in zip(before, net.parameters())))
        # Stage 2: the critic scores patches; edge-smoothed art keeps its size.
        art = torch.rand(2, 3, 64, 64) * 2 - 1
        self.assertEqual(train.smooth_edges(art).shape, art.shape)
        self.assertEqual(critic(art).dim(), 4)

    def test_art_needs_its_licence(self) -> None:
        import train
        from PIL import Image

        with tempfile.TemporaryDirectory() as d:
            Image.new("RGB", (80, 80), (200, 100, 50)).save(f"{d}/a.png")
            with self.assertRaises(SystemExit):
                train.Art(d, 64, 0)
            Path(d, "LICENCE.txt").write_text("Commissioned from the artist for Photo Plus, contract #1")
            self.assertEqual(train.Art(d, 64, 0).batch(2).shape, (2, 3, 64, 64))


class DataTest(unittest.TestCase):
    def test_a_photo_without_a_licence_is_refused(self) -> None:
        import data

        with tempfile.TemporaryDirectory() as d:
            p = Path(d, "m.jsonl")
            p.write_text(json.dumps({"image": "a.jpg"}) + "\n")
            with self.assertRaises(SystemExit):
                data.read_manifest(str(p))
            p.write_text(json.dumps({"image": "a.jpg", "licence": "model release 1", "split": "val"}) + "\n")
            self.assertEqual(data.read_manifest(str(p))[0]["split"], "val")

    def test_fit_keeps_multiples_of_eight(self) -> None:
        import data
        from PIL import Image

        im = data.fit(Image.new("RGB", (1000, 750)), 512)
        self.assertEqual(im.size, (512, 384))
        self.assertTrue(all(s % 8 == 0 for s in data.fit(Image.new("RGB", (333, 999)), 500).size))


class ExportTest(unittest.TestCase):
    def test_onnx_gives_the_same_picture_in_the_apps_layout(self) -> None:
        import export

        torch.manual_seed(1)
        net = ToonNet(16, 2).eval()
        with torch.no_grad():
            for p in net.film.parameters():
                p.normal_(0, 0.1)
        model = export.AppLayout(net).eval()
        with tempfile.TemporaryDirectory() as d:
            path = Path(d, "t.onnx")
            export.to_onnx(model, 64, path)
            self.assertLess(export.check_onnx(model, path, 64), 1e-3)
            import onnx

            g = onnx.load(str(path)).graph
            self.assertEqual([i.name for i in g.input], ["image", "mask", "cond"])
            dims = [[d.dim_value for d in i.type.tensor_type.shape.dim] for i in g.input]
            self.assertEqual(dims, [[1, 64, 64, 3], [1, 64, 64, 1], [1, cond.COND]])


if __name__ == "__main__":
    unittest.main()
