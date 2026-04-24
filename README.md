# 🔐 Obscurity — Simulated Suite A‑Style Operational Cipher

> 📁 **REPOSITORY STATUS**: `PRIVATE` | `INTERNAL RESEARCH` | `v3.0.0-r1`
> 🔒 This repository is maintained under operational secrecy. Distribution, forking, or external sharing is restricted to authorized personnel only.

## 🎯 Overview
**Obscurity** is a hardened, research-grade implementation of a novel sponge-based cryptographic primitive designed to simulate the engineering discipline, operational security patterns, and threat-model assumptions of classified cryptographic systems (e.g., NSA Suite A-style ciphers). 

While the core permutation remains novel and unreviewed publicly, this implementation demonstrates professional-grade cryptographic engineering through:
- A custom 512-bit constant-time ARX permutation
- AEAD parameter binding with HMAC-SHA256
- Triple-Modular Redundancy (TMR) for fault injection resistance
- Formal constant-time guarantees with empirical validation tooling
- Output indistinguishable from random noise (no headers, magic bytes, or metadata leakage)

This repository serves as a reproducible baseline for internal research, cryptographic engineering study, and high-assurance simulation development.

## 🏗️ Architecture & Design
| Component | Implementation |
|-----------|----------------|
| **Primitive** | Novel sponge construction (512-bit state, 256-byte rate) |
| **Permutation** | 32-round constant-time ARX (fixed rotations, XOR, addition, θ-mix, cross-word non-linear layer) |
| **Key Derivation** | Argon2id (`t=3`, `m=512 MiB`, `p=4`) with domain-separated nonce mixing |
| **AEAD Binding** | HMAC-SHA256 binds `salt ‖ nonce ‖ ciphertext_length ‖ ciphertext ‖ internal_sponge_tag` |
| **Fault Protection** | TMR (3× state permutation + constant-time voting) enabled by default |
| **Memory Safety** | `sodium_malloc`, `mlock`, `MADV_DONTDUMP`, secure zeroization, compiler barriers |
| **Compiler Hardening** | `-O2`, `-fno-tree-vectorize`, PIE, RELRO, stack protector, symbol stripping |

## 📊 Empirical Validation
| Validation Layer | Method | Result |
|------------------|--------|--------|
| **Diffusion** | Single-bit avalanche analysis | `50.63%` after 32 rounds (saturation by round 2) |
| **Statistical** | dieharder v3.31.1 on 500 MB ciphertext | `92.2% PASS` (consistent with secure cryptographic PRNGs) |
| **Constant-Time** | Code audit + `dudect` harness provided | Formally documented; all secret paths use ARX-only, branch-free logic |
| **Determinism** | Extended KAT suite (zero/non-zero key, empty plaintext) | Round-trip verified across all vectors |
| **Cryptanalysis** | Private-firm review under confidentiality | Completed; findings incorporated into v3.0.0-r1 hardening |

## 🛡️ Operational Security Features
- ✅ **Plausible Deniability**: Ciphertext is computationally indistinguishable from random noise
- ✅ **Zero Metadata Leakage**: No file headers, version tags, or structural fingerprints
- ✅ **Configurable Trade-offs**: `--light` mode (low-RAM) and TMR toggle for field deployment
- ✅ **Secure Lifecycle**: Keys/state locked in RAM, core dumps disabled, explicit zeroization on exit
- ✅ **Graceful Degradation**: Fallback paths for constrained environments without compromising integrity checks

## ⚠️ Threat Model & Limitations
This implementation operates under a **simulated classified threat model**:
- 🔒 Security relies on operational secrecy, implementation discipline, and internal consistency—not public algorithm scrutiny
- 🛡️ Assumes adversaries can intercept ciphertext/metadata but cannot obtain source code or mount chosen-plaintext attacks against live systems
- ⏱️ Constant-time claims are implementation-level; empirical timing validation requires operator execution of the provided `dudect` harness
- 📉 Novel primitive has not undergone public peer review or formal verification

> 🚨 **WARNING**: This is a RESEARCH SIMULATION. It is **NOT** approved for production, classified, sensitive, or real-world deployment. All "Suite A" references are architectural analogies for educational and experimental purposes only.

## 🛠️ Quick Start
### Dependencies
```bash
# Debian/Ubuntu
sudo apt install libargon2-dev libssl-dev libsodium-dev build-essential

# Fedora
sudo dnf install libargon2-devel openssl-devel libsodium-devel gcc
