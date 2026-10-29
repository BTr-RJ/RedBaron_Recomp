import tempfile
from pathlib import Path
import unittest
from prepare_deploy import prepare


class DeployTests(unittest.TestCase):
    def test_config_generation_and_preservation(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "private"
            self.assertEqual(prepare("play.example.org", "8.8.8.8", output), "wss://play.example.org")
            environment = dict(line.split("=", 1) for line in (output / "rooms.env").read_text().splitlines())
            self.assertEqual(len(environment["TURN_SECRET"]), 64)
            self.assertIn("static-auth-secret=" + environment["TURN_SECRET"], (output / "turnserver.conf").read_text())
            with self.assertRaises(FileExistsError):
                prepare("play.example.org", "8.8.8.8", output)
            self.assertIn(environment["TURN_SECRET"], (output / "rooms.env").read_text())

    def test_invalid_addresses_do_not_create_files(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "private"
            for domain, address in (("bad\nconfig", "8.8.8.8"), ("play.example.org", "192.168.1.2"),
                                    ("play.example.org", "127.0.0.1")):
                with self.assertRaises(ValueError):
                    prepare(domain, address, output)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
