#!/usr/bin/env python3
# Copyright (c) 2026 David Bertet. Licensed under the MIT License.
#
# Sign a release flash manifest for on-device OTA verification.
#
# The device checks ECDSA-P256/SHA256 over canonical JSON before flashing
# anything (see todo #6). Canonical form: the manifest WITHOUT the
# signature/key_id fields, json.dumps(sort_keys=True, separators=(',',':')).
# The device recomputes exactly this, so the signer and firmware must agree
# on field layout — keep both sides in sync.
#
# Signing key: P-256 private key in PEM. The real one lives in GitHub
# Secrets (release.yml); mint throwaway dev keys with `genkey`.
#
#   genkey:    sign_manifest.py genkey --out devkey.pem [--pubkey-out ...]
#   sign:      sign_manifest.py sign --manifest m.json --key key.pem [--key-id 0]
#   verify:    sign_manifest.py verify --manifest m.json --pubkey pub.pem
"""Release manifest signer (ECDSA-P256, DER signature, base64 in manifest)."""

import argparse
import base64
import json
import sys

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


def canonical_bytes(manifest):
    unsigned = {k: v for k, v in manifest.items() if k not in ("signature", "key_id")}
    return json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()


def load_private_key(path):
    with open(path, "rb") as f:
        return serialization.load_pem_private_key(f.read(), password=None)


def load_public_key(path):
    with open(path, "rb") as f:
        return serialization.load_pem_public_key(f.read())


def cmd_genkey(args):
    key = ec.generate_private_key(ec.SECP256R1())
    with open(args.out, "wb") as f:
        f.write(
            key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        )
    pub = args.pubkey_out or args.out + ".pub"
    with open(pub, "wb") as f:
        f.write(
            key.public_key().public_bytes(
                serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
            )
        )
    print(f"private: {args.out}\npublic:  {pub}")


def cmd_sign(args):
    with open(args.manifest) as f:
        manifest = json.load(f)
    key = load_private_key(args.key)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        sys.exit("key is not a P-256 private key")
    der = key.sign(canonical_bytes(manifest), ec.ECDSA(hashes.SHA256()))
    manifest["signature"] = base64.b64encode(der).decode()
    manifest["key_id"] = args.key_id
    with open(args.manifest, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"signed {args.manifest} with key_id={args.key_id}")


def cmd_verify(args):
    with open(args.manifest) as f:
        manifest = json.load(f)
    try:
        der = base64.b64decode(manifest["signature"])
    except (KeyError, ValueError):
        sys.exit("manifest has no (valid) signature field")
    pub = load_public_key(args.pubkey)
    try:
        pub.verify(der, canonical_bytes(manifest), ec.ECDSA(hashes.SHA256()))
    except InvalidSignature:
        sys.exit("BAD signature")
    print(f"OK (key_id={manifest.get('key_id', '?')})")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(required=True)
    gen = sub.add_parser("genkey", help="mint a throwaway P-256 keypair")
    gen.add_argument("--out", required=True)
    gen.add_argument("--pubkey-out", default=None)
    gen.set_defaults(func=cmd_genkey)
    sign = sub.add_parser("sign", help="sign a manifest in place")
    sign.add_argument("--manifest", required=True)
    sign.add_argument("--key", required=True)
    sign.add_argument("--key-id", type=int, default=0)
    sign.set_defaults(func=cmd_sign)
    verify = sub.add_parser("verify", help="verify a signed manifest")
    verify.add_argument("--manifest", required=True)
    verify.add_argument("--pubkey", required=True)
    verify.set_defaults(func=cmd_verify)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
