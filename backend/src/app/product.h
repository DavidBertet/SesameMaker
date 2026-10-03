// Product identity shim. Core code must never hardcode the product name so
// core/ stays byte-identical across products (plain copy to sync); only
// app/product.h differs per product.

#pragma once

#define PRODUCT_NAME "SesameMaker"

// Self-update source (on-device OTA): release download base URL and the
// manifest file prefix (full name: <prefix>-<idf-target>-manifest.json).
// The signing key below is the production ECDSA-P256 public key (public by
// design); OTA_PUBKEY_HEX overrides it for local testing.
#define PRODUCT_OTA_BASE_DEFAULT "https://github.com/DavidBertet/SesameMaker/releases/latest/download"
#define PRODUCT_MANIFEST_PREFIX "sesamemaker"
#define PRODUCT_OTA_PUBKEY_DEFAULT "0c8c9b162c9fcf3e446d3ecdcce1590e9c5785e3e1f5be3da9ad7335aa75384ee849750fbd29f9433d3676e95feb4a36ded9a43ff209ad03dcdd405dfebd3b23"
