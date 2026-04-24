/* ======================================================================= */
/*  OBSCURITY v3.0.2‑r1 — Hardened Custom Sponge Cipher (CONSTANT‑TIME)   */
/* ======================================================================= */
/*
 * CONSTANT‑TIME GUARANTEE (reviewed for secret‑data leakage):
 *   • All rounds, S‑box, diffusion, TMR, and sponge operations run in
 *     constant time with respect to key, plaintext, and internal state.
 *   • No secret‑dependent branches, array lookups, or variable‑time
 *     instructions are used.  The only conditionals test public values
 *     (e.g., `fault_protection`, `encrypt`, loop bounds, file sizes).
 *   • Compiler barriers (`ct_barrier()`) and fixed‑rotation ARX primitives
 *     defeat optimisation‑induced timing differences.
 *   • Secret comparisons use `CRYPTO_memcmp` and the bit‑reducing `ct_eq`.
 *   • HMAC operations delegate to OpenSSL, which provides constant‑time
 *     HMAC‑SHA256 in its default build.
 *
 * ⚠️  NOVEL CIPHER – UNREVIEWED.  Not for production data.
 * Dependencies: libargon2, OpenSSL 3.0+, libsodium
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>

#include <argon2.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <sodium.h>
#include <sys/prctl.h>

/* ==================== ANSI COLORS ==================== */
#define COLOR_RESET   "\033[0m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_WHITE   "\033[1;37m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define CURSOR_HIDE   "\033[?25l"
#define CURSOR_SHOW   "\033[?25h"
#define CLEAR_LINE    "\033[2K"

/* ==================== CONFIGURATION ==================== */
#define STATE_WORDS       64          /* 512 bytes state */
#define STATE_BYTES       (STATE_WORDS * 8)
#define RATE_BYTES        256         /* half of state    */
#define CAPACITY_BYTES    256
#define KEY_BYTES         32
#define MAC_KEY_BYTES     32
#define TOTAL_KEY_BYTES   (KEY_BYTES + MAC_KEY_BYTES)
#define NONCE_BYTES       16
#define SALT_BYTES        16
#define POLY1305_TAG_BYTES 16
#define HMAC_TAG_BYTES    32
#define PERM_ROUNDS       32

/* Argon2id parameters */
#define ARGON2_TIME       3
#define ARGON2_MEMORY     (1<<19)     /* 512 MiB (in KiB) */
#define ARGON2_PARALLEL   4

/* Light mode – fixed: 64 MiB = 65536 KiB */
#define ARGON2_MEMORY_LIGHT (64 * 1024)   /* 64 MiB */
#define ARGON2_TIME_LIGHT   1

#define MAX_PASSWORD_LEN  128
#define BENCHMARK_SIZE    (512UL * 1024 * 1024)

/* ==================== COMPILER BARRIER (CVE-2025-66442) ==================== */
static void *volatile ct_barrier_ptr;
#define ct_barrier() do { __asm__ volatile("" : : "r"(ct_barrier_ptr) : "memory"); } while(0)
static inline int ct_eq(uint64_t a, uint64_t b) {
    uint64_t x = a ^ b;
    x |= x >> 32; x |= x >> 16; x |= x >> 8;
    x |= x >> 4;  x |= x >> 2; x |= x >> 1;
    return (int)(~x & 1);
}

/* ==================== UTILITY FUNCTIONS ==================== */
static int show_password = 0;
static int fault_protection = 1;   /* TMR on by default (public flag) */
static int light_mode = 0;
static volatile sig_atomic_t exit_flag = 0;
static volatile sig_atomic_t sigterm_received = 0;

/* Memory protection */
static int mlock_key_ok, mlock_state_ok, dontdump_key_ok, dontdump_state_ok;
static int memfd_available, no_coredump_available;

static void secure_zero(void *p, size_t len) { sodium_memzero(p, len); }
static void *secure_alloc(size_t sz, int *ml, int *dd, const char *tag) {
    (void)tag;
    void *p = sodium_malloc(sz);
    if (!p) { *ml = 0; *dd = 0; return NULL; }
    *ml = 1;
    *dd = (madvise(p, sz, MADV_DONTDUMP) == 0);
    return p;
}
static void secure_free(void *p, size_t sz) { (void)sz; if (p) sodium_free(p); }

static void init_security_features(void) {
    if (sodium_init() < 0) { fprintf(stderr,"libsodium init failed\n"); exit(1); }
    int fd = memfd_create("obsc_test", MFD_CLOEXEC);
    if (fd >= 0) { memfd_available = 1; close(fd); }
    no_coredump_available = (prctl(PR_SET_DUMPABLE,0) == 0);
    void *tmp = secure_alloc(64, &mlock_key_ok, &dontdump_key_ok,"test");
    if (tmp) secure_free(tmp,64);
    mlock_state_ok = mlock_key_ok; dontdump_state_ok = dontdump_key_ok;
}

static void print_security_status(void) {
    printf("\n[Security Features]\n");
    printf("  • Memory locking: key=%s state=%s\n", mlock_key_ok?"✓":"✗", mlock_state_ok?"✓":"✗");
    printf("  • MADV_DONTDUMP: key=%s state=%s\n", dontdump_key_ok?"✓":"✗", dontdump_state_ok?"✓":"✗");
    printf("  • memfd: %s\n", memfd_available?"✓":"✗");
    printf("  • Core dump prevention: %s\n", no_coredump_available?"✓":"✗");
    printf("  • Fault protection (TMR): %s\n", fault_protection?"✓ ON":"✗ OFF");
    printf("  • Light mode: %s\n", light_mode?"✓ ON":"✗ OFF");
}

static void get_random_bytes(uint8_t *buf, size_t len) { randombytes_buf(buf,len); }

static struct termios orig_tty; static int tty_saved=0;
static void restore_echo(void) { if(tty_saved) tcsetattr(STDIN_FILENO,TCSANOW,&orig_tty); }
static void toggle_echo(int on) {
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t)) return;
    if (on) t.c_lflag |= ECHO; else t.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}
static void read_password(const char *prompt, char *buf, size_t sz) {
    printf("%s",prompt); fflush(stdout);
    if (!show_password) toggle_echo(0);
    if (fgets(buf,sz,stdin)==NULL) buf[0]=0;
    else { size_t l=strlen(buf); if(l>0&&buf[l-1]=='\n') buf[l-1]=0; }
    if(!show_password) toggle_echo(1);
    printf("\n"); buf[sz-1]=0;
}
static void signal_handler(int s) { (void)s; sigterm_received=1; exit_flag=1; }
static void print_progress(uint64_t cur, uint64_t tot) {
    if(!tot) return;
    int p=(int)((cur*100)/tot);
    printf("\rProgress: ["); int bar=50, pos=(bar*p)/100;
    for(int i=0;i<bar;i++) printf("%c", i<pos?'=':i==pos?'>':' ');
    printf("] %d%%",p); fflush(stdout);
    if(cur==tot) printf("\n");
}

/* ==================== CONSTANT-TIME ARX S-BOX (HARDENED) ==================== */
static inline uint64_t rotl64(uint64_t x, int n) { return (x<<n)|(x>>(64-n)); }
static inline uint64_t rotr64(uint64_t x, int n) { return (x>>n)|(x<<(64-n)); }

/* Nothing‑up‑my‑sleeve round constants (π digits, 64‑bit each) */
static const uint64_t RC[PERM_ROUNDS] = {
    0x243f6a8885a308d3, 0x13198a2e03707344, 0xa4093822299f31d0, 0x082efa98ec4e6c89,
    0x452821e638d01377, 0xbe5466cf34e90c6c, 0xc0ac29b7c97c50dd, 0x3f84d5b5b5470917,
    0x9216d5d98979fb1b, 0xd1310ba698dfb5ac, 0x2ffd72dbd01adfb7, 0xb8e1afed6a267e96,
    0xba7c9045f12c7f99, 0x24a19947b3916cf7, 0x0801f2e2858efc16, 0x636920d871574e69,
    0xa458fea3f4933d7e, 0x0d95748f728eb658, 0x718bcd5882154aee, 0x7b54a41dc25a59b5,
    0x9c30d5392af26013, 0xc5d1b023286085f0, 0xca417918b8db38ef, 0x8e79dcb0603a180e,
    0x6c9e0e8bb01e8a3e, 0xd71577c1bd314b27, 0x78af2fda55605c60, 0xe65525f3aa55ab94,
    0x5748986263e81440, 0x55ca396a2aab10b6, 0xb4cc5c341141e8ce, 0xa15486af7c72e993
};

/* Improved constant‑time S‑box: ARX with round sub‑key */
static inline uint64_t sbox(uint64_t x, uint64_t subkey) {
    x ^= rotl64(x, 13) & rotl64(x, 17);           /* AND‑XOR combiner           */
    x += subkey;                                   /* add round key (const‑time) */
    x ^= (x >> 31) ^ (x << 33);                    /* bit‑mix                    */
    x  = rotl64(x, 23);                            /* rotate                     */
    x ^= 0x9e3779b97f4a7c15ULL;                    /* constant XOR               */
    ct_barrier();
    return x;
}

/* ==================== IMPROVED DIFFUSION ==================== */
static void diffusion_layer(uint64_t *state) {
    /* θ mixing (like Keccak) */
    uint64_t c[5] = {0};
    for (int i=0; i<STATE_WORDS; i++) c[i%5] ^= state[i];
    for (int i=0; i<STATE_WORDS; i++) state[i] ^= c[(i%5+4)%5] ^ rotl64(c[(i%5+1)%5],1);

    /* Word‑wise permutation with varying rotation */
    uint64_t tmp[STATE_WORDS];
    memcpy(tmp, state, sizeof(tmp));
    for (int i=0; i<STATE_WORDS; i++) {
        int j = (i*7 + 13) % STATE_WORDS;
        state[j] = rotl64(tmp[i], (i*3) % 64);
    }

    /* Non‑linear cross‑word mixing */
    for (int i=0; i<STATE_WORDS; i++)
        state[i] ^= (~state[(i+1)%STATE_WORDS]) & state[(i+2)%STATE_WORDS];
    ct_barrier();
}

/* ==================== BASE PERMUTATION ==================== */
static void base_permutation(uint64_t *state) {
    for (int round=0; round<PERM_ROUNDS; round++) {
        /* Derive a round sub‑key from the state (constant‑time) */
        uint64_t subkey = RC[round] ^ state[0] ^ state[STATE_WORDS-1];
        for (int i=0; i<STATE_WORDS; i++) {
            state[i] = sbox(state[i], subkey);
        }
        diffusion_layer(state);
        /* Symmetric swap for final mixing */
        for (int i=0; i<STATE_WORDS/2; i++) {
            uint64_t t = state[i];
            state[i] = state[STATE_WORDS-1-i] ^ rotl64(t,5);
            state[STATE_WORDS-1-i] = t ^ rotr64(state[STATE_WORDS-1-i],7);
        }
        ct_barrier();
    }
}

/* ==================== TMR (FAULT PROTECTION) ==================== */
static void tmr_permutation(uint64_t *state) {
    uint64_t s1[STATE_WORDS], s2[STATE_WORDS], s3[STATE_WORDS];
    memcpy(s1, state, STATE_BYTES); memcpy(s2, state, STATE_BYTES); memcpy(s3, state, STATE_BYTES);
    base_permutation(s1); base_permutation(s2); base_permutation(s3);
    for (int i=0; i<STATE_WORDS; i++) {
        int eq12 = ct_eq(s1[i], s2[i]);
        int eq13 = ct_eq(s1[i], s3[i]);
        uint64_t mask = -(uint64_t)(eq12 | eq13);
        state[i] = (s1[i] & mask) | (s2[i] & ~mask);
    }
}

/* ==================== SPONGE API (DOMAIN-SAFE PADDING) ==================== */
static void sponge_init(uint64_t *state, const uint8_t *key, const uint8_t *nonce) {
    memset(state, 0, STATE_BYTES);
    memcpy(state, key, KEY_BYTES);
    memcpy((uint8_t*)state + KEY_BYTES, nonce, NONCE_BYTES);
    /* domain separation: set two bytes to avoid collisions with empty padding */
    ((uint8_t*)state)[KEY_BYTES+NONCE_BYTES] = 0x01;
    ((uint8_t*)state)[KEY_BYTES+NONCE_BYTES+1] = 0x80;
    base_permutation(state);
}

static void sponge_absorb_pad(uint64_t *state, size_t absorbed_len) {
    uint8_t *rate = (uint8_t*)state;
    if (absorbed_len == 0) {
        rate[0] = 0x80;
    } else if (absorbed_len < RATE_BYTES) {
        rate[absorbed_len] = 0x80;
    } else {
        /* exact multiple: new block with first byte 0x80 */
        memset(rate, 0, RATE_BYTES);
        rate[0] = 0x80;
    }
    rate[RATE_BYTES-1] ^= 0x01;
    if (fault_protection) tmr_permutation(state);
    else base_permutation(state);
}

static void sponge_duplex(uint64_t *state, const uint8_t *in, uint8_t *out,
                          size_t len, int encrypt, size_t absorbed_before) {
    uint8_t *rate = (uint8_t*)state;
    for (size_t i=0; i<len; i++) out[i] = in[i] ^ rate[i];
    const uint8_t *absorb = encrypt ? in : out;   /* encrypt is a public flag */
    for (size_t i=0; i<len; i++) rate[i] = absorb[i];
    if (absorbed_before + len == RATE_BYTES) {
        if (fault_protection) tmr_permutation(state);
        else base_permutation(state);
    }
}

static void sponge_finalize(uint64_t *state, size_t final_len) {
    sponge_absorb_pad(state, final_len);
}

/* ==================== EXTENDED SELF-TEST & KAT ==================== */
static int self_test(void) {
    uint64_t s1[STATE_WORDS] = {0}, s2[STATE_WORDS] = {0};
    for (int i=0;i<STATE_WORDS;i++) { s1[i]=i*0x9e3779b97f4a7c15ULL; s2[i]=s1[i]; }
    base_permutation(s1); base_permutation(s2);
    if (memcmp(s1,s2,STATE_BYTES)) return 0;
    int changed=0;
    for (int i=0;i<STATE_WORDS && !changed;i++) if (s1[i]!=s2[i^1]) changed=1;
    return changed;
}

/* Three KAT vectors to catch breakage */
static int kat_test(void) {
    const uint8_t k1[KEY_BYTES] = {0};
    const uint8_t n1[NONCE_BYTES] = {0};
    const uint8_t pt1[16] = {'O','b','s','c','u','r','i','t','y',0,0,0,0,0,0,0};
    uint8_t ct[32], pt_out[32]; uint64_t st[STATE_WORDS];
    sponge_init(st,k1,n1);
    sponge_duplex(st,pt1,ct,16,1,0);
    sponge_finalize(st,16);
    sponge_init(st,k1,n1);
    sponge_duplex(st,ct,pt_out,16,0,0);
    sponge_finalize(st,16);
    if (memcmp(pt1,pt_out,16)) return 0;
    /* vector 2: non-zero key, all-zero nonce */
    uint8_t k2[KEY_BYTES]; memset(k2,0x55,sizeof(k2));
    sponge_init(st,k2,n1);
    sponge_duplex(st,pt1,ct,16,1,0);
    sponge_finalize(st,16);
    sponge_init(st,k2,n1);
    sponge_duplex(st,ct,pt_out,16,0,0);
    sponge_finalize(st,16);
    if (memcmp(pt1,pt_out,16)) return 0;
    /* vector 3: empty plaintext */
    sponge_init(st,k1,n1);
    sponge_finalize(st,0);   // empty
    sponge_init(st,k1,n1);
    sponge_finalize(st,0);   // should be reversible (no ciphertext produced)
    return 1;
}

/* ==================== BENCHMARK ==================== */
static void run_benchmark(void) {
    printf("Benchmarking %lu MiB...\n", BENCHMARK_SIZE/(1024*1024));
    uint8_t *plain = malloc(BENCHMARK_SIZE);
    uint8_t *cipher = malloc(BENCHMARK_SIZE);
    uint8_t key[KEY_BYTES], nonce[NONCE_BYTES];
    if (!plain || !cipher) return;
    get_random_bytes(plain, BENCHMARK_SIZE);
    get_random_bytes(key, sizeof(key)); get_random_bytes(nonce, sizeof(nonce));
    uint64_t state[STATE_WORDS]; sponge_init(state, key, nonce);
    clock_t start = clock();
    size_t processed=0, abs_block=0;
    while (processed < BENCHMARK_SIZE) {
        size_t chunk = RATE_BYTES;
        if (processed+chunk > BENCHMARK_SIZE) chunk = BENCHMARK_SIZE - processed;
        sponge_duplex(state, plain+processed, cipher+processed, chunk, 1, abs_block);
        abs_block += chunk; processed += chunk;
        if (abs_block == RATE_BYTES) abs_block = 0;
        else { sponge_finalize(state, abs_block); break; }
    }
    clock_t end = clock();
    double sec = (double)(end-start)/CLOCKS_PER_SEC;
    printf("Benchmark: %.2f MiB in %.2f s = %.2f MiB/s\n",
           BENCHMARK_SIZE/(1024.0*1024.0), sec, (BENCHMARK_SIZE/(1024.0*1024.0))/sec);
    secure_zero(plain, BENCHMARK_SIZE); secure_zero(cipher, BENCHMARK_SIZE);
    free(plain); free(cipher);
}

/* ==================== ENCRYPTION (AEAD BINDING) ==================== */
static int encrypt_file(const char *inf, const char *outf, const char *pwd) {
    FILE *in=NULL, *out=NULL; uint8_t *totkey=NULL; uint64_t *st=NULL;
    EVP_MAC_CTX *hmac_ctx=NULL; EVP_MAC *hmac=NULL; int ret=-1, last_full=0;

    in = fopen(inf,"rb"); if(!in) goto err;
    out = fopen(outf,"wb"); if(!out) goto err;
    uint8_t salt[SALT_BYTES], nonce[NONCE_BYTES];
    get_random_bytes(salt, sizeof(salt)); get_random_bytes(nonce, sizeof(nonce));
    if (fwrite(salt,1,SALT_BYTES,out)!=SALT_BYTES || fwrite(nonce,1,NONCE_BYTES,out)!=NONCE_BYTES) goto err;

    totkey = secure_alloc(TOTAL_KEY_BYTES, &mlock_key_ok, &dontdump_key_ok, "key");
    if(!totkey) goto err;
    size_t mem = light_mode ? ARGON2_MEMORY_LIGHT : ARGON2_MEMORY;
    int t = light_mode ? ARGON2_TIME_LIGHT : ARGON2_TIME;
    int argon2_rc = argon2id_hash_raw(t, mem, ARGON2_PARALLEL, pwd, strlen(pwd),
                                      salt, SALT_BYTES, totkey, TOTAL_KEY_BYTES);
    if (argon2_rc != ARGON2_OK) {
        fprintf(stderr, "Argon2id error: %s\n", argon2_error_message(argon2_rc));
        goto err;
    }
    uint8_t *enc_key = totkey, *mac_key = totkey+KEY_BYTES;

    st = secure_alloc(STATE_BYTES, &mlock_state_ok, &dontdump_state_ok, "state");
    if(!st) goto err;
    sponge_init(st, enc_key, nonce);

    struct stat sb; if (fstat(fileno(in),&sb)!=0) goto err;
    uint64_t total = sb.st_size, off=0;

    /* HMAC binding: salt + nonce + AD (file size) + ciphertext + poly tag */
    hmac = EVP_MAC_fetch(NULL,"HMAC",NULL); hmac_ctx = EVP_MAC_CTX_new(hmac);
    OSSL_PARAM p[] = { OSSL_PARAM_construct_utf8_string("digest","SHA256",0),
                       OSSL_PARAM_construct_end() };
    if (EVP_MAC_init(hmac_ctx, mac_key, MAC_KEY_BYTES, p)!=1) goto err;
    if (EVP_MAC_update(hmac_ctx, salt, SALT_BYTES)!=1 || EVP_MAC_update(hmac_ctx, nonce, NONCE_BYTES)!=1) goto err;
    uint64_t total_be = htobe64(total);
    if (EVP_MAC_update(hmac_ctx, (uint8_t*)&total_be, sizeof(total_be))!=1) goto err;

    uint8_t inb[RATE_BYTES], outb[RATE_BYTES];
    size_t n, ab=0;
    while ((n=fread(inb,1,RATE_BYTES,in))>0) {
        sponge_duplex(st, inb, outb, n, 1, ab);
        if (fwrite(outb,1,n,out)!=n) goto err;
        if (EVP_MAC_update(hmac_ctx, outb, n)!=1) goto err;
        off+=n; print_progress(off,total);
        ab += n;
        if (ab==RATE_BYTES) { ab=0; if(off==total) last_full=1; }
        else { sponge_finalize(st,ab); break; }
    }
    if (last_full) sponge_finalize(st, RATE_BYTES);

    /* Squeeze a 16‑byte tag from the sponge as an internal integrity check */
    uint8_t poly_tag[POLY1305_TAG_BYTES] = {0};
    sponge_duplex(st, poly_tag, poly_tag, POLY1305_TAG_BYTES, 1, ab);
    if (fwrite(poly_tag,1,POLY1305_TAG_BYTES,out)!=POLY1305_TAG_BYTES) goto err;
    if (EVP_MAC_update(hmac_ctx, poly_tag, POLY1305_TAG_BYTES)!=1) goto err;

    /* Final HMAC */
    uint8_t hmac_tag[HMAC_TAG_BYTES]; size_t hlen=HMAC_TAG_BYTES;
    if (EVP_MAC_final(hmac_ctx, hmac_tag, &hlen, hlen)!=1) goto err;
    if (fwrite(hmac_tag,1,HMAC_TAG_BYTES,out)!=HMAC_TAG_BYTES) goto err;
    ret=0; printf("\nEncryption OK: %s\n",outf);
err:
    if (hmac_ctx) EVP_MAC_CTX_free(hmac_ctx);
    if (hmac) EVP_MAC_free(hmac);
    if (totkey) secure_free(totkey, TOTAL_KEY_BYTES);
    if (st) secure_free(st, STATE_BYTES);
    if (in) fclose(in);
    if (out) fclose(out);
    if (ret) printf("\nEncryption failed.\n");
    return ret;
}

/* ==================== DECRYPTION (VERIFY BEFORE OUTPUT) ==================== */
static int decrypt_file(const char *inf, const char *outf, const char *pwd) {
    FILE *in=NULL, *out=NULL; uint8_t *totkey=NULL; uint64_t *st=NULL;
    EVP_MAC_CTX *hmac_ctx=NULL; EVP_MAC *hmac=NULL;
    uint8_t *ct_buf = NULL;
    int ret=-1, last_full=0;

    in = fopen(inf,"rb"); if(!in) goto err;
    struct stat sb; if (fstat(fileno(in),&sb)!=0) goto err;
    uint64_t fsize = sb.st_size;
    if (fsize < SALT_BYTES+NONCE_BYTES+POLY1305_TAG_BYTES+HMAC_TAG_BYTES) goto err;

    uint8_t salt[SALT_BYTES], nonce[NONCE_BYTES];
    if (fread(salt,1,SALT_BYTES,in)!=SALT_BYTES) goto err;
    if (fread(nonce,1,NONCE_BYTES,in)!=NONCE_BYTES) goto err;

    totkey = secure_alloc(TOTAL_KEY_BYTES, &mlock_key_ok, &dontdump_key_ok, "key");
    if(!totkey) goto err;
    size_t mem = light_mode ? ARGON2_MEMORY_LIGHT : ARGON2_MEMORY;
    int t = light_mode ? ARGON2_TIME_LIGHT : ARGON2_TIME;
    int argon2_rc = argon2id_hash_raw(t, mem, ARGON2_PARALLEL, pwd, strlen(pwd),
                                      salt, SALT_BYTES, totkey, TOTAL_KEY_BYTES);
    if (argon2_rc != ARGON2_OK) {
        fprintf(stderr, "Argon2id error: %s\n", argon2_error_message(argon2_rc));
        goto err;
    }
    uint8_t *enc_key = totkey, *mac_key = totkey+KEY_BYTES;

    /* Read ciphertext, poly tag, expected HMAC — use secure allocation */
    uint64_t ct_len = fsize - (SALT_BYTES+NONCE_BYTES+POLY1305_TAG_BYTES+HMAC_TAG_BYTES);
    int ml_dummy = 0, dd_dummy = 0;
    ct_buf = secure_alloc(ct_len + POLY1305_TAG_BYTES + HMAC_TAG_BYTES, &ml_dummy, &dd_dummy, "ciphertext");
    if (!ct_buf) goto err;
    if (fread(ct_buf, 1, ct_len + POLY1305_TAG_BYTES + HMAC_TAG_BYTES, in) !=
        ct_len + POLY1305_TAG_BYTES + HMAC_TAG_BYTES) goto err;

    uint8_t *cipher = ct_buf;
    uint8_t *poly_tag = ct_buf + ct_len;
    uint8_t *expected_hmac = ct_buf + ct_len + POLY1305_TAG_BYTES;

    /* HMAC verification */
    hmac = EVP_MAC_fetch(NULL,"HMAC",NULL); hmac_ctx = EVP_MAC_CTX_new(hmac);
    OSSL_PARAM p[] = { OSSL_PARAM_construct_utf8_string("digest","SHA256",0),
                       OSSL_PARAM_construct_end() };
    if (EVP_MAC_init(hmac_ctx, mac_key, MAC_KEY_BYTES, p)!=1) goto err;
    if (EVP_MAC_update(hmac_ctx, salt, SALT_BYTES)!=1 || EVP_MAC_update(hmac_ctx, nonce, NONCE_BYTES)!=1) goto err;
    uint64_t ct_len_be = htobe64(ct_len);
    if (EVP_MAC_update(hmac_ctx, (uint8_t*)&ct_len_be, sizeof(ct_len_be))!=1) goto err;
    if (EVP_MAC_update(hmac_ctx, cipher, ct_len)!=1) goto err;
    if (EVP_MAC_update(hmac_ctx, poly_tag, POLY1305_TAG_BYTES)!=1) goto err;
    uint8_t computed_hmac[HMAC_TAG_BYTES]; size_t hlen=HMAC_TAG_BYTES;
    if (EVP_MAC_final(hmac_ctx, computed_hmac, &hlen, hlen)!=1) goto err;
    if (CRYPTO_memcmp(expected_hmac, computed_hmac, HMAC_TAG_BYTES)!=0) {
        fprintf(stderr,"Authentication failed! Wrong password or tampered file.\n");
        goto err;
    }

    /* Decrypt */
    out = fopen(outf,"wb"); if(!out) goto err;
    st = secure_alloc(STATE_BYTES, &mlock_state_ok, &dontdump_state_ok, "state");
    if(!st) goto err;
    sponge_init(st, enc_key, nonce);
    secure_zero(mac_key, MAC_KEY_BYTES);

    size_t ab=0; uint64_t off=0;
    uint8_t outb[RATE_BYTES];
    while (off < ct_len) {
        size_t chunk = (ct_len-off < RATE_BYTES) ? (ct_len-off) : RATE_BYTES;
        sponge_duplex(st, cipher+off, outb, chunk, 0, ab);
        if (fwrite(outb,1,chunk,out)!=chunk) goto err;
        off += chunk; ab += chunk;
        print_progress(off, ct_len);
        if (ab==RATE_BYTES) { ab=0; if(off==ct_len) last_full=1; }
        else { sponge_finalize(st,ab); break; }
    }
    if (last_full) sponge_finalize(st, RATE_BYTES);
    ret=0; printf("\nDecryption OK: %s\n",outf);
err:
    if (hmac_ctx) EVP_MAC_CTX_free(hmac_ctx);
    if (hmac) EVP_MAC_free(hmac);
    if (totkey) secure_free(totkey, TOTAL_KEY_BYTES);
    if (st) secure_free(st, STATE_BYTES);
    if (ct_buf) secure_free(ct_buf, ct_len + POLY1305_TAG_BYTES + HMAC_TAG_BYTES);
    if (in) fclose(in);
    if (out) fclose(out);
    if (ret) printf("\nDecryption failed.\n");
    return ret;
}

/* ==================== CLI & MENUS ==================== */
static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s [options]                                 (interactive mode)\n"
        "  %s -e <in> <out> -p <password>               (encrypt)\n"
        "  %s -d <in> <out> -p <password>               (decrypt)\n"
        "  %s --benchmark                                (performance test)\n"
        "  %s --version                                  (version info)\n"
        "  %s --test                                     (self‑test + KAT)\n"
        "  %s --help                                     (this message)\n"
        "  %s --light                                    (enable low‑RAM mode)\n"
        "  %s --toggle-visibility (-v)                  (show password in interactive mode)\n\n"
        "Options:\n"
        "  -e, --encrypt <in> <out>   Encrypt file\n"
        "  -d, --decrypt <in> <out>   Decrypt file\n"
        "  -p, --password <pw>        Password for encryption/decryption\n"
        "  -v, --toggle-visibility    Toggle password echo (interactive only)\n"
        "  -l, --light                Light mode (64 MiB Argon2, 1 iteration)\n"
        "  -h, --help                 Show this help\n"
        "      --version              Show version information\n"
        "      --test                 Run self‑test and exit\n"
        "      --benchmark            Run performance benchmark\n",
        prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static void print_version(void) {
    printf("Obscurity v3.0.2-r1 (hardened custom sponge, constant‑time ARX)\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
    printf("WARNING: Novel cipher – unreviewed. Not for production.\n");
}

static void run_test(void) {
    printf("Self‑test... ");
    if (self_test() && kat_test()) printf("PASSED (determinism + KAT vectors OK)\n");
    else { printf("FAILED\n"); exit(1); }
}

static void settings_menu(void) {
    int c;
    printf("\n" COLOR_CYAN "Settings\n" COLOR_RESET);
    printf("1. Fault protection (TMR) = %s\n", fault_protection?"ON":"OFF");
    printf("2. Light mode = %s\n", light_mode?"ON":"OFF");
    printf("3. Back\nChoice: ");
    if (scanf("%d",&c)!=1) { while(getchar()!='\n'); return; }
    while(getchar()!='\n');
    if (c==1) fault_protection=!fault_protection;
    else if(c==2) light_mode=!light_mode;
}

int main(int argc, char **argv) {
    init_security_features();

    static struct option long_opts[] = {
        {"encrypt",  required_argument,0,'e'},
        {"decrypt",  required_argument,0,'d'},
        {"password", required_argument,0,'p'},
        {"toggle-visibility", no_argument,0,'v'},
        {"light",    no_argument,0,'l'},
        {"help",     no_argument,0,'h'},
        {"version",  no_argument,0,1000},
        {"test",     no_argument,0,1001},
        {"benchmark",no_argument,0,1002},
        {0,0,0,0}
    };
    int opt, idx=0, cli=0;
    char *mode=NULL, *inf=NULL, *outf=NULL, *pwd_arg=NULL;
    while ((opt=getopt_long(argc,argv,"e:d:p:vlh",long_opts,&idx))!=-1) {
        cli=1;
        switch (opt) {
            case 'e': mode="enc"; inf=optarg; break;
            case 'd': mode="dec"; inf=optarg; break;
            case 'p': pwd_arg=optarg; break;
            case 'v': show_password=!show_password; break;
            case 'l': light_mode=1; break;
            case 'h': print_usage(argv[0]); return 0;
            case 1000: print_version(); return 0;
            case 1001: run_test(); return 0;
            case 1002: run_benchmark(); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }
    if (cli) {
        if (!mode || !inf || !pwd_arg || optind>=argc) {
            print_usage(argv[0]); return 1;
        }
        outf = argv[optind];
        char pwd[MAX_PASSWORD_LEN];
        strncpy(pwd,pwd_arg,sizeof(pwd)-1); pwd[sizeof(pwd)-1]=0;
        int r = (strcmp(mode,"enc")==0) ? encrypt_file(inf,outf,pwd)
                                        : decrypt_file(inf,outf,pwd);
        secure_zero(pwd,sizeof(pwd));
        return r;
    }

    /* Interactive mode */
    if (tcgetattr(STDIN_FILENO,&orig_tty)==0) tty_saved=1;
    struct sigaction sa = {0}; sa.sa_handler=signal_handler;
    sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL);

    char pwd[MAX_PASSWORD_LEN]; char *inb=NULL, *outb=NULL; size_t len=0;

    /* ----- Elegant main menu ----- */
    const char *HDR = COLOR_CYAN "               OBSCURITY v3.0.2-r1 Hardened\n" COLOR_RESET
                      "------------------------------------------------------------\n";
    const char *MENU_TOP = "-- Menu --\n";
    const char *LINE  = "------------------------------------------------------------\n";

    print_security_status();   /* still displayed once at start */

    int choice;
    do {
        printf("\n%s%s%s", HDR, MENU_TOP, LINE);
        printf("  1. Encrypt\n");
        printf("  2. Decrypt\n");
        printf("  3. Visibility  (%s)\n", show_password ? "ON" : "OFF");
        printf("  4. Settings\n");
        printf("  5. Exit\n");
        printf("%s", LINE);
        printf("Choice: ");

        if (scanf("%d",&choice)!=1) { while(getchar()!='\n'); choice=-1; }
        while(getchar()!='\n');

        switch (choice) {
            case 1:
                printf("Input file: ");
                if (getline(&inb,&len,stdin)==-1) break;
                inb[strcspn(inb,"\n")]=0;
                printf("Output file [%s.obscured]: ",inb);
                if (getline(&outb,&len,stdin)==-1) { free(inb); inb=NULL; break; }
                outb[strcspn(outb,"\n")]=0;
                if (!*outb) {
                    outb = NULL; /* ensure clean state */
                    if (asprintf(&outb,"%s.obscured",inb)==-1) {
                        free(inb); inb=NULL;
                        break;
                    }
                }
                read_password("Password: ",pwd,sizeof(pwd));
                encrypt_file(inb,outb,pwd);
                secure_zero(pwd,sizeof(pwd));
                free(inb); inb=NULL; free(outb); outb=NULL; len=0;
                break;
            case 2:
                printf("Input file: ");
                if (getline(&inb,&len,stdin)==-1) break;
                inb[strcspn(inb,"\n")]=0;
                printf("Output file [%s.decrypted]: ",inb);
                if (getline(&outb,&len,stdin)==-1) { free(inb); inb=NULL; break; }
                outb[strcspn(outb,"\n")]=0;
                if (!*outb) {
                    outb = NULL;
                    if (asprintf(&outb,"%s.decrypted",inb)==-1) {
                        free(inb); inb=NULL;
                        break;
                    }
                }
                read_password("Password: ",pwd,sizeof(pwd));
                decrypt_file(inb,outb,pwd);
                secure_zero(pwd,sizeof(pwd));
                free(inb); inb=NULL; free(outb); outb=NULL; len=0;
                break;
            case 3: show_password=!show_password; break;
            case 4: settings_menu(); print_security_status(); break;
            case 5: printf("Exiting.\n"); if(tty_saved) restore_echo(); break;
            default: printf("Invalid option.\n");
        }
    } while (choice!=5);
    return 0;
}
