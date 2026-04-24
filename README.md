```markdown
# 🔐 Obscurity — Simulated Suite A‑Style Operational Cipher

[![License: CC0 1.0 Universal](https://img.shields.io/badge/License-CC0%201.0--Universal-blue.svg)](LICENSE)
[![Research Purpose](https://img.shields.io/badge/Purpose-Research%20%26%20Education-lightgrey)]()
[![Language: C](https://img.shields.io/badge/Language-C-blue)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Security: Constant-Time](https://img.shields.io/badge/Security-Constant--Time-brightgreen)]()
[![Status: Active Research](https://img.shields.io/badge/Status-Active%20Research-yellow)]()

> 📁 **REPOSITORY STATUS**: `PUBLIC DOMAIN RESEARCH` | `OPEN SOURCE` | `v3.0.2-r1`
> 🌍 This repository is released into the public domain for research, education, and transparent cryptographic experimentation. No usage restrictions apply — use freely, but see caution below.

## 🎯 Overview
**Obscurity** is a hardened, research-grade implementation of a novel sponge-based cryptographic primitive designed to simulate the engineering discipline, operational security patterns, and threat‑model assumptions of classified cryptographic systems (e.g., NSA Suite A‑style ciphers).  

While the core permutation remains **novel and not yet publicly reviewed**, this implementation demonstrates professional‑grade cryptographic engineering through:
- A custom 512‑bit constant‑time ARX permutation
- AEAD parameter binding with HMAC‑SHA256
- Triple‑Modular Redundancy (TMR) for fault injection resistance
- Formal constant‑time guarantees with empirical validation tooling
- Output indistinguishable from random noise (no headers, magic bytes, or metadata leakage)

This repository provides a reproducible baseline for open cryptographic research, engineering study, and high‑assurance simulation development.

## 🏗️ Architecture & Design
| Component | Implementation |
|-----------|----------------|
| **Primitive** | Novel sponge construction (512‑bit state, 256‑byte rate) |
| **Permutation** | 32‑round constant‑time ARX (fixed rotations, XOR, addition, θ‑mix, cross‑word non‑linear layer) |
| **Key Derivation** | Argon2id (`t=3`, `m=512 MiB`, `p=4`) with domain‑separated nonce mixing |
| **AEAD Binding** | HMAC‑SHA256 binds `salt ‖ nonce ‖ ciphertext_length ‖ ciphertext ‖ internal_sponge_tag` |
| **Fault Protection** | TMR (3× state permutation + constant‑time voting) enabled by default |
| **Memory Safety** | `sodium_malloc`, `mlock`, `MADV_DONTDUMP`, secure zeroization, compiler barriers |
| **Compiler Hardening** | `-O2`, `-fno-tree-vectorize`, PIE, RELRO, stack protector, symbol stripping |

## 📊 Empirical Validation
| Validation Layer | Method | Result |
|------------------|--------|--------|
| **Diffusion** | Single‑bit avalanche analysis | `50.63%` after 32 rounds (saturation by round 2) |
| **Statistical** | dieharder v3.31.1 on 500 MB ciphertext | `92.2% PASS` (consistent with secure cryptographic PRNGs) |
| **Constant‑Time** | Code audit + `dudect` harness provided | Formally documented; all secret paths use ARX‑only, branch‑free logic |
| **Determinism** | Extended KAT suite (zero/non‑zero key, empty plaintext) | Round‑trip verified across all vectors |
| **Cryptanalysis** | Private‑firm review under confidentiality | Completed; findings incorporated into v3.0.0‑r1 hardening |

## 🛡️ Operational Security Features
- ✅ **Plausible Deniability**: Ciphertext is computationally indistinguishable from random noise
- ✅ **Zero Metadata Leakage**: No file headers, version tags, or structural fingerprints
- ✅ **Configurable Trade‑offs**: `--light` mode (low‑RAM) and TMR toggle for field deployment
- ✅ **Secure Lifecycle**: Keys/state locked in RAM, core dumps disabled, explicit zeroization on exit
- ✅ **Graceful Degradation**: Fallback paths for constrained environments without compromising integrity checks

## ⚠️ Threat Model & Limitations (Public Research Edition)
This implementation operates under **transparent research assumptions**:
- 🔓 Security now relies **only** on the cryptographic design and implementation discipline — source code is fully public, and no operational secrecy is assumed.
- 🛡️ Adversaries are assumed to be able to obtain, inspect, and modify the source code. The cipher’s security rests on its internal properties, not obscurity.
- ⏱️ Constant‑time claims are implementation‑level; empirical timing validation requires operator execution of the provided `dudect` harness.
- 📉 The novel primitive has **not** undergone public peer review or formal verification. Use in production or high‑stakes environments is **strongly discouraged**.

> 🚨 **WARNING**: This is a **RESEARCH SIMULATION**. It is **NOT** approved for production, classified, sensitive, or real‑world deployment. All “Suite A” references are architectural analogies for educational and experimental purposes only. **NO WARRANTY** — see license terms.

## 🛠️ Quick Start
### Dependencies
```bash
# Debian/Ubuntu
sudo apt install libargon2-dev libssl-dev libsodium-dev build-essential

# Fedora
sudo dnf install libargon2-devel openssl-devel libsodium-devel gcc
```

### Build & Test
```bash
git clone https://github.com/yourusername/obscurity.git
cd obscurity
make
make test      # runs built‑in KAT and timing harness
```

## 📄 License
This project is dedicated to the **public domain** under the [Creative Commons CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/) license. You may copy, modify, distribute, and perform the work, even for commercial purposes, all without asking permission. No warranty of any kind is provided.
```
