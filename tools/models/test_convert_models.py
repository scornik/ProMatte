import tempfile
import unittest
from pathlib import Path

from convert_models import paddle_model_dir


class PaddleModelDirectoryTests(unittest.TestCase):
    def test_ignores_macos_metadata_before_real_model(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            metadata = root / "__MACOSX" / "model"
            metadata.mkdir(parents=True)
            for name in ("._model.pdmodel", "._model.pdiparams"):
                (metadata / name).touch()
            model = root / "inference"
            model.mkdir()
            for name in ("model.pdmodel", "model.pdiparams"):
                (model / name).touch()
            self.assertEqual(Path(paddle_model_dir(root)), model)

    def test_requires_parameters(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "model.pdmodel").touch()
            with self.assertRaises(SystemExit):
                paddle_model_dir(tmp)

    def test_rejects_metadata_only_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            metadata = Path(tmp) / "__MACOSX"
            metadata.mkdir()
            for name in ("model.pdmodel", "model.pdiparams"):
                (metadata / name).touch()
            with self.assertRaises(SystemExit):
                paddle_model_dir(tmp)


if __name__ == "__main__":
    unittest.main()
