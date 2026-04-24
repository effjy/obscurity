# 🔐 Obscurity — Simulated Suite A‑Style Operational Cipher

🏷️ Badges: [License: CC0 1.0] [Purpose: Research & Education] [Language: C] [Security: Constant-Time] [Status: Active Research]

📁 REPOSITORY STATUS: PUBLIC DOMAIN RESEARCH | OPEN SOURCE | v3.0.2-r1  
🌍 This repo is public domain. Use freely for research, education, or just for fun — but read the warning below.

## 🎯 Overview

Obscurity is a research-grade, sponge-based cipher that mimics the engineering style of classified systems (like NSA Suite A). It's not a real classified cipher — it's a simulation built to show high-quality crypto engineering.

What it does:
- 🔁 A custom 512‑bit ARX permutation (constant‑time)
- 🔗 AEAD with HMAC‑SHA256 binding
- 🛡️ Triple‑Modular Redundancy (TMR) against fault injection
- ⏱️ Formally constant‑time with a test harness
- 📁 No metadata, no magic bytes — output looks like random noise

This is a baseline for open crypto research, not a production cipher.

## 🏗️ Architecture at a glance

- Primitive: Sponge, 512‑bit state, 256‑byte rate  
- Permutation: 32 rounds ARX (rotations, XOR, addition, θ‑mix)  
- Key derivation: Argon2id (t=3, 512 MiB memory, 4 threads)  
- AEAD binding: HMAC‑SHA256 over salt, nonce, ciphertext length, ciphertext, and internal tag  
- Fault protection: Triple‑redundant permutation with voting (on by default)  
- Memory safety: sodium_malloc, mlock, MADV_DONTDUMP, secure zeroization  
- Compiler hardening: -O2, PIE, RELRO, stack protector, symbol stripping

## 📊 Validation results (real tests)

- 🧨 Diffusion after 32 rounds: 50.63% (saturated by round 2)  
- 🎲 dieharder on 500 MB ciphertext: 92.2% PASS (good for a secure PRNG)  
- ⏲️ Constant‑time: audited + dudect harness — branch‑free on secret data  
- ✔️ Determinism: KAT suite passes (zero/non‑zero key, empty plaintext)  
- 🔍 Private cryptanalysis review: completed, fixes in v3.0.0‑r1

## 🛡️ Operational security features

- 🕵️ Plausible deniability: ciphertext = random noise  
- 🚫 Zero metadata leakage: no headers, no version tags  
- ⚙️ Configurable: --light mode (low RAM) and TMR toggle  
- 🔒 Secure lifecycle: keys locked in RAM, core dumps off, explicit zeroization  
- 🧩 Graceful degradation: fallbacks without breaking integrity

## ⚠️ Public research threat model

Because this is now public domain, we assume:

- 🔓 Security relies purely on the crypto design and implementation — no secrecy of source code  
- 🧠 Adversaries can see, copy, and modify the code  
- ⏱️ Constant‑time claims need you to run the dudect harness yourself  
- 📚 The novel permutation has NOT been publicly peer‑reviewed or formally verified  

🚨 WARNING: This is a RESEARCH SIMULATION. Do NOT use for real secrets, classified data, or production systems. All "Suite A" references are for education only. No warranty.

## 🛠️ Quick start

Install dependencies (Debian/Ubuntu):

sudo apt install libargon2-dev libssl-dev libsodium-dev build-essential

Or Fedora:

sudo dnf install libargon2-devel openssl-devel libsodium-devel gcc

Then build:

git clone https://github.com/yourusername/obscurity.git  
cd obscurity  
make  
make test

## 📄 License

Public domain under CC0 1.0 Universal. Do whatever you want — copy, modify, sell, ignore. No warranty, no liability.
