# Copyright (c) 2026 David Bertet. Licensed under the MIT License.
"""Round-trip tests for sign_manifest.py (stdlib unittest, no pytest needed)."""

import base64
import copy
import json
import os
import subprocess
import sys
import tempfile
import unittest

TOOL = os.path.join(os.path.dirname(__file__), "sign_manifest.py")

MANIFEST = {
    "version": "v9.9.9-test",
    "chip": "ESP32-C6",
    "files": [{"name": "a.bin", "offset": "0x0", "size": 3, "sha256": "abc"}],
}


def run(*argv):
    return subprocess.run(
        [sys.executable, TOOL, *argv], capture_output=True, text=True, check=False
    )


class SignManifestTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.key = os.path.join(self.tmp.name, "dev.pem")
        self.pub = os.path.join(self.tmp.name, "dev.pem.pub")
        self.manifest = os.path.join(self.tmp.name, "m.json")
        with open(self.manifest, "w") as f:
            json.dump(MANIFEST, f)
        r = run("genkey", "--out", self.key, "--pubkey-out", self.pub)
        self.assertEqual(r.returncode, 0, r.stderr)

    def tearDown(self):
        self.tmp.cleanup()

    def test_sign_verify_round_trip(self):
        r = run("sign", "--manifest", self.manifest, "--key", self.key, "--key-id", "1")
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(self.manifest) as f:
            signed = json.load(f)
        self.assertEqual(signed["key_id"], 1)
        base64.b64decode(signed["signature"])  # valid base64 DER
        r = run("verify", "--manifest", self.manifest, "--pubkey", self.pub)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("OK", r.stdout)

    def test_tampered_manifest_fails_verify(self):
        run("sign", "--manifest", self.manifest, "--key", self.key)
        with open(self.manifest) as f:
            signed = json.load(f)
        signed["files"][0]["size"] = 999
        with open(self.manifest, "w") as f:
            json.dump(signed, f)
        r = run("verify", "--manifest", self.manifest, "--pubkey", self.pub)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("BAD", r.stderr + r.stdout)

    def test_wrong_key_fails_verify(self):
        other = os.path.join(self.tmp.name, "other.pem")
        other_pub = os.path.join(self.tmp.name, "other.pem.pub")
        run("genkey", "--out", other, "--pubkey-out", other_pub)
        run("sign", "--manifest", self.manifest, "--key", self.key)
        r = run("verify", "--manifest", self.manifest, "--pubkey", other_pub)
        self.assertNotEqual(r.returncode, 0)

    def test_canonical_bytes_are_golden(self):
        # The firmware rebuilds these exact bytes in ota_update.c
        # (ota_canonical). If this changes, the C side must change with it
        # or every signature fails.
        sys.path.insert(0, os.path.dirname(os.path.abspath(TOOL)))
        from sign_manifest import canonical_bytes

        payload = canonical_bytes(
            {
                "version": "v1.0.0",
                "chip": "ESP32-C6",
                "files": [{"name": "f.bin", "offset": "0x0", "size": 100, "sha256": "aa"}],
                "app": {"name": "a.bin", "offset": "ota", "size": 200, "sha256": "bb"},
                "signature": "ignored",
                "key_id": 7,
            }
        )
        self.assertEqual(
            payload,
            b'{"app":{"name":"a.bin","offset":"ota","sha256":"bb","size":200},'
            b'"chip":"ESP32-C6",'
            b'"files":[{"name":"f.bin","offset":"0x0","sha256":"aa","size":100}],'
            b'"version":"v1.0.0"}',
        )

    def test_sign_is_deterministic_input_stable(self):
        # Same content signed twice verifies both times (ECDSA randomness is
        # fine — what matters is the canonical bytes are stable).
        run("sign", "--manifest", self.manifest, "--key", self.key)
        with open(self.manifest) as f:
            first = json.load(f)["signature"]
        unsigned = copy.deepcopy(MANIFEST)
        with open(self.manifest, "w") as f:
            json.dump(unsigned, f)
        run("sign", "--manifest", self.manifest, "--key", self.key)
        r = run("verify", "--manifest", self.manifest, "--pubkey", self.pub)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(first)


if __name__ == "__main__":
    unittest.main()
