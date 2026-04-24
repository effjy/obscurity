/* diffusion_demo.c — Visualize diffusion in Obscurity's permutation use ./diffusion_demo 8 -t to test 8 rounds */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* === COPY THESE FROM obscurity.c === */
#define STATE_WORDS 64
#define STATE_BYTES (STATE_WORDS * 8)
#define PERM_ROUNDS 32

static inline uint64_t rotl64(uint64_t x, int n) { return (x<<n)|(x>>(64-n)); }
static inline uint64_t rotr64(uint64_t x, int n) { return (x>>n)|(x<<(64-n)); }

static void *volatile ct_barrier_ptr;
#define ct_barrier() do { __asm__ volatile("" : : "r"(ct_barrier_ptr) : "memory"); } while(0)

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

static inline uint64_t sbox(uint64_t x, uint64_t subkey) {
    x ^= rotl64(x, 13) & rotl64(x, 17);
    x += subkey;
    x ^= (x >> 31) ^ (x << 33);
    x  = rotl64(x, 23);
    x ^= 0x9e3779b97f4a7c15ULL;
    ct_barrier();
    return x;
}

static void diffusion_layer(uint64_t *state) {
    uint64_t c[5] = {0};
    for (int i=0; i<STATE_WORDS; i++) c[i%5] ^= state[i];
    for (int i=0; i<STATE_WORDS; i++) state[i] ^= c[(i%5+4)%5] ^ rotl64(c[(i%5+1)%5],1);
    uint64_t tmp[STATE_WORDS];
    memcpy(tmp, state, sizeof(tmp));
    for (int i=0; i<STATE_WORDS; i++) {
        int j = (i*7 + 13) % STATE_WORDS;
        state[j] = rotl64(tmp[i], (i*3) % 64);
    }
    for (int i=0; i<STATE_WORDS; i++)
        state[i] ^= (~state[(i+1)%STATE_WORDS]) & state[(i+2)%STATE_WORDS];
    ct_barrier();
}

static void base_permutation_round(uint64_t *state, int round) {
    uint64_t subkey = RC[round] ^ state[0] ^ state[STATE_WORDS-1];
    for (int i=0; i<STATE_WORDS; i++) state[i] = sbox(state[i], subkey);
    diffusion_layer(state);
    for (int i=0; i<STATE_WORDS/2; i++) {
        uint64_t t = state[i];
        state[i] = state[STATE_WORDS-1-i] ^ rotl64(t,5);
        state[STATE_WORDS-1-i] = t ^ rotr64(state[STATE_WORDS-1-i],7);
    }
    ct_barrier();
}

/* === DIFFUSION ANALYSIS UTILS === */
static int hamming_weight64(uint64_t x) {
    int c = 0;
    while(x) { c += x&1; x >>= 1; }
    return c;
}

static int hamming_distance(const uint64_t *a, const uint64_t *b) {
    int diff = 0;
    for(int i=0; i<STATE_WORDS; i++) diff += hamming_weight64(a[i] ^ b[i]);
    return diff;
}

static void print_bit_diff_heatmap(const uint64_t *a, const uint64_t *b, int width) {
    printf("\nBit-difference heatmap (first %d words, %d bits each):\n", width, 64);
    printf("  ");
    for(int col=0; col<64; col+=8) printf("%2d ", col);
    printf("\n  ");
    for(int col=0; col<64; col++) printf(col%8==0?"|":".");
    printf("|\n");
    for(int row=0; row<width && row<8; row++) {
        printf("%2d|", row);
        uint64_t diff = a[row] ^ b[row];
        for(int bit=0; bit<64; bit++) {
            /* ASCII-safe characters to avoid -Wmultichar */
            printf("%c", (diff >> (63-bit)) & 1 ? '#' : '.');
            if((bit+1)%8==0) printf(" ");
        }
        printf("\n");
    }
    printf("  Legend: # = bit flipped, . = unchanged\n");
}

/* === MAIN DEMO === */
int main(int argc, char **argv) {
    int rounds = (argc > 1) ? atoi(argv[1]) : PERM_ROUNDS;
    int trace = (argc > 2 && strcmp(argv[2], "-t") == 0);
    
    printf("=== Obscurity Diffusion Demo ===\n");
    printf("State: %d words × 64 bits = %d bits total\n", STATE_WORDS, STATE_BYTES*8);
    printf("Rounds: %d | Trace mode: %s\n\n", rounds, trace?"ON":"OFF");

    uint64_t state_a[STATE_WORDS] = {0};
    uint64_t state_b[STATE_WORDS] = {0};
    
    /* Initialize with deterministic pattern */
    for(int i=0; i<STATE_WORDS; i++) state_a[i] = (uint64_t)i * 0x9e3779b97f4a7c15ULL;
    memcpy(state_b, state_a, STATE_BYTES);
    
    /* Flip a single bit: word 0, bit 0 */
    state_b[0] ^= (1ULL << 0);
    
    printf("Input difference: 1 bit (state[0] bit 0)\n");
    printf("Initial Hamming distance: %d / %d bits (%.2f%%)\n\n", 
           hamming_distance(state_a, state_b), STATE_BYTES*8,
           100.0 * hamming_distance(state_a, state_b) / (STATE_BYTES*8));

    if(trace) printf("Round-by-round avalanche:\n");
    
    for(int r=0; r<rounds; r++) {
        base_permutation_round(state_a, r);
        base_permutation_round(state_b, r);
        
        int hd = hamming_distance(state_a, state_b);
        double pct = 100.0 * hd / (STATE_BYTES*8);
        
        if(trace) {
            printf("  Round %2d: %4d bits flipped (%5.2f%%) ", r, hd, pct);
            if(pct > 45 && pct < 55) printf("✓ near-ideal");
            else if(pct < 30) printf("⚠ under-diffused");
            else if(pct > 70) printf("⚠ over-concentrated");
            printf("\n");
        }
    }
    
    int final_hd = hamming_distance(state_a, state_b);
    double final_pct = 100.0 * final_hd / (STATE_BYTES*8);
    
    printf("\n=== Results ===\n");
    printf("Final Hamming distance: %d / %d bits (%.2f%%)\n", 
           final_hd, STATE_BYTES*8, final_pct);
    
    if(final_pct >= 45 && final_pct <= 55) {
        printf("✓ Excellent avalanche: ~50%% bit flip indicates strong diffusion\n");
    } else if(final_pct >= 40 && final_pct <= 60) {
        printf("✓ Good avalanche: within acceptable statistical bounds\n");
    } else {
        printf("⚠ Avalanche outside ideal range — consider reviewing diffusion layer\n");
    }
    
    /* Optional: show which words were most affected */
    printf("\nTop 5 most-changed 64-bit words:\n");
    int changes[STATE_WORDS];
    for(int i=0; i<STATE_WORDS; i++) changes[i] = hamming_weight64(state_a[i] ^ state_b[i]);
    for(int pass=0; pass<5; pass++) {
        int max_idx = -1, max_val = -1;
        for(int i=0; i<STATE_WORDS; i++) {
            if(changes[i] > max_val) { max_val = changes[i]; max_idx = i; }
        }
        if(max_idx >= 0) {
            printf("  word[%2d]: %2d bits flipped\n", max_idx, max_val);
            changes[max_idx] = -1;
        }
    }
    
    /* Optional heatmap for first few words */
    if(rounds >= PERM_ROUNDS) {
        print_bit_diff_heatmap(state_a, state_b, 8);
    }
    
    return 0;
}
