import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1] / "controller"


def record(alt):
    return (f"@ GPS_STAT 1 2 3 4 00:01 CRC_OK TRK mock Alt {alt} "
            "lt 19.25 ln -98.5 Vel 0 0 0 Fix 3 # 1 2 3 4@").encode()


class ControllerTest(unittest.TestCase):
    def test_internal_events(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "state-test"
            subprocess.run(["cc", "-I", ROOT, ROOT / "state.c",
                            Path(__file__).with_name("controller_state.c"), "-o", exe], check=True)
            subprocess.run([exe], check=True)

    def test_mock_receivers_and_dashboard(self):
        subprocess.run(["make", "-C", ROOT, "controller"], check=True)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dashboard.sock"
            pipes = [Path(directory) / name for name in ("first", "second")]
            for pipe in pipes:
                os.mkfifo(pipe)
            with subprocess.Popen([ROOT / "build" / "controller", path, *pipes]) as proc:
                try:
                    deadline = time.monotonic() + 3
                    while not path.exists():
                        self.assertIsNone(proc.poll())
                        self.assertLess(time.monotonic(), deadline)
                        time.sleep(.01)
                    with socket.socket(socket.AF_UNIX) as client:
                        client.settimeout(2)
                        client.connect(str(path))
                        with client.makefile("rb") as stream:
                            def message():
                                return json.loads(stream.readline())
                            initial = message()
                            self.assertEqual(initial["state"], "DISARMED")
                            self.assertEqual(len(initial["receivers"]), 2)
                            client.sendall(b'"ARM"\n"START_MANUAL"\n')
                            self.assertFalse(message()["accepted"])
                            self.assertTrue(message()["accepted"])
                            self.assertEqual(message()["state"], "MANUAL_ACQUISITION")
                            first = record(100)
                            with pipes[0].open("wb", buffering=0) as a, pipes[1].open("wb", buffering=0) as b:
                                a.write(first[:30])
                                b.write(record(200))
                                samples = message()["receivers"]
                                self.assertIsNone(samples[0]["telemetry"])
                                self.assertAlmostEqual(samples[1]["telemetry"]["altitude_m"], 60.96)
                                a.write(first[30:])
                                samples = message()["receivers"]
                                self.assertEqual([r["id"] for r in samples], [1, 2])
                                self.assertAlmostEqual(samples[0]["telemetry"]["altitude_m"], 30.48)
                    with socket.socket(socket.AF_UNIX) as client:
                        client.settimeout(2)
                        client.connect(str(path))
                        with client.makefile("rb") as stream:
                            snapshot = json.loads(stream.readline())
                        self.assertEqual(snapshot["state"], "MANUAL_ACQUISITION")
                        self.assertEqual(snapshot["receivers"], samples)
                finally:
                    proc.terminate()
                    proc.wait(timeout=3)


if __name__ == "__main__":
    unittest.main()
