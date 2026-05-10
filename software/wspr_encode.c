/*
 * wspr_encode.c
 *
 * WSPR message encoder - encodes callsign, grid, power into 162 4-FSK symbols.
 * Follows the same logic as the reference Python encoder.
 *
 * Usage (standalone test):
 *   gcc -o wspr_encode wspr_encode.c && ./wspr_encode VK2AAX QF56 37
 *
 * To use as a library in wspr_transmit.c:
 *   int symbols[162];
 *   encode_wspr("VK2AAX", "QF56", 37, symbols);
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <assert.h>

/* -------------------------------------------------------------------------
 * Protocol constants
 * ---------------------------------------------------------------------- */
#define WSPR_SYMBOLS   162
#define POLY1          0xF2D05351UL
#define POLY2          0xE4613C47UL

/* 162-bit sync vector - same as pr3[] in wsprd.c */
static const uint8_t SYNC_VECTOR[WSPR_SYMBOLS] = {
    1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,1,1,1,1,0,0,0,0,0,
    0,0,1,0,0,1,0,1,0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,1,0,1,0,
    0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,
    0,0,1,0,0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,0,1,0,0,0,1,1,1,
    0,0,0,0,0,1,0,1,0,0,1,1,0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
    0,0
};

static const char CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
#define NCHARS 37  /* 10 digits + 26 letters + space */

/* -------------------------------------------------------------------------
 * Helper: find character index in CHARS[]
 * ---------------------------------------------------------------------- */
static int char_index(char c)
{
    c = toupper((unsigned char)c);
    for (int i = 0; i < NCHARS; i++)
        if (CHARS[i] == c) return i;
    return -1;  /* not found */
}

/* -------------------------------------------------------------------------
 * Step 1: encode callsign → 28-bit integer
 * Mirrors Python _encode_callsign()
 * ---------------------------------------------------------------------- */
static uint32_t encode_callsign(const char *call_in)
{
    char call[8];
    memset(call, ' ', sizeof(call));

    /* uppercase copy */
    char tmp[8];
    int len = 0;
    for (int i = 0; call_in[i] && i < 6; i++)
        tmp[len++] = toupper((unsigned char)call_in[i]);
    tmp[len] = '\0';

    /*
     * If 2nd character is a digit the prefix is 1 letter (e.g. G4JNT):
     * prepend a space so the field is ' G4JNT'.
     * Otherwise it's a 2-letter prefix (VK2XX, K1ABC): pad right with spaces.
     */
    if (len >= 2 && isdigit((unsigned char)tmp[1])) {
        call[0] = ' ';
        for (int i = 0; i < len && i < 5; i++) call[i+1] = tmp[i];
    } else {
        for (int i = 0; i < len && i < 6; i++) call[i] = tmp[i];
    }
    /* ensure 6 chars */
    for (int i = 0; i < 6; i++)
        if (call[i] == '\0') call[i] = ' ';

    uint32_t n;
    n  = char_index(call[0]);
    n  = n * 36 + char_index(call[1]);
    n  = n * 10 + char_index(call[2]);
    n  = n * 27 + (char_index(call[3]) - 10);
    n  = n * 27 + (char_index(call[4]) - 10);
    n  = n * 27 + (char_index(call[5]) - 10);
    return n;
}

/* -------------------------------------------------------------------------
 * Step 2: encode locator + power → 22-bit integer
 * Mirrors Python _encode_locator_power()
 * ---------------------------------------------------------------------- */
static uint32_t encode_locator_power(const char *loc, int power)
{
    char l[5];
    for (int i = 0; i < 4; i++) l[i] = toupper((unsigned char)loc[i]);
    l[4] = '\0';

    uint32_t m;
    m  = (179 - 10 * (l[0] - 'A') - (l[2] - '0')) * 180;
    m += 10 * (l[1] - 'A') + (l[3] - '0');
    m  = m * 128 + power + 64;
    return m;
}

/* -------------------------------------------------------------------------
 * Step 3: pack 28-bit N and 22-bit M into 81 bits (50 data + 31 zeros)
 * Mirrors Python _pack_50_bits()
 * ---------------------------------------------------------------------- */
static void pack_50_bits(uint32_t n, uint32_t m, uint8_t bits[81])
{
    /* combined is a 50-bit value: n occupies bits 49..22, m bits 21..0 */
    uint64_t combined = ((uint64_t)n << 22) | m;
    for (int i = 0; i < 50; i++)
        bits[i] = (combined >> (49 - i)) & 1;
    for (int i = 50; i < 81; i++)
        bits[i] = 0;
}

/* -------------------------------------------------------------------------
 * Step 4: convolutional FEC encoder (rate 1/2, K=32)
 * Mirrors Python _conv_encode()
 * ---------------------------------------------------------------------- */
static int parity32(uint32_t x)
{
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4;
    x ^= x >> 2;  x ^= x >> 1;
    return x & 1;
}

static void conv_encode(const uint8_t bits[81], uint8_t coded[WSPR_SYMBOLS])
{
    uint32_t reg = 0;
    int out = 0;
    for (int i = 0; i < 81; i++) {
        reg = ((reg << 1) | bits[i]) & 0xFFFFFFFFUL;
        coded[out++] = parity32(reg & POLY1);
        coded[out++] = parity32(reg & POLY2);
    }
    /* out == 162 */
}

/* -------------------------------------------------------------------------
 * Step 5: interleaver (bit-reversal permutation)
 * Mirrors Python _interleave()
 * ---------------------------------------------------------------------- */
static int bit_reverse_8(int i)
{
    int r = 0;
    for (int b = 0; b < 8; b++)
        r |= ((i >> b) & 1) << (7 - b);
    return r;
}

static void interleave(const uint8_t coded[WSPR_SYMBOLS],
                       uint8_t interleaved[WSPR_SYMBOLS])
{
    memset(interleaved, 0, WSPR_SYMBOLS);
    int p = 0;
    for (int i = 0; i < 256 && p < WSPR_SYMBOLS; i++) {
        int j = bit_reverse_8(i);
        if (j < WSPR_SYMBOLS) {
            interleaved[j] = coded[p++];
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API: encode_wspr()
 *
 * Fills symbols[162] with values 0-3.
 * Returns 0 on success, -1 on error.
 * ---------------------------------------------------------------------- */
int encode_wspr(const char *callsign, const char *grid,
                int power, uint8_t symbols[WSPR_SYMBOLS])
{
    /* basic validation */
    if (!callsign || !grid || strlen(grid) < 4) return -1;
    if (power < 0 || power > 60)               return -1;

    uint32_t n = encode_callsign(callsign);
    uint32_t m = encode_locator_power(grid, power);

    uint8_t bits[81];
    pack_50_bits(n, m, bits);

    uint8_t coded[WSPR_SYMBOLS];
    conv_encode(bits, coded);

    uint8_t interleaved[WSPR_SYMBOLS];
    interleave(coded, interleaved);

    /* combine sync vector with data: symbol = sync + 2*data */
    for (int i = 0; i < WSPR_SYMBOLS; i++)
        symbols[i] = SYNC_VECTOR[i] + 2 * interleaved[i];

    return 0;
}

/* -------------------------------------------------------------------------
 * Standalone test: print symbols and verify against known output
 * ---------------------------------------------------------------------- */
#ifdef STANDALONE_TEST

/*
 * Known-good symbol sequence for VK2AAX / QF56 / 37
 * Generated from the reference Python encoder.
 * Use this to verify the C encoder is correct.
 */
static const uint8_t KNOWN_VK2AAX_QF56_37[WSPR_SYMBOLS] = {
    3,1,2,2,0,2,2,0,1,0,0,2,1,1,3,2,2,0,3,0,
    2,3,0,3,3,3,3,0,2,0,2,0,0,0,3,2,0,1,0,3,
    2,2,0,2,0,0,1,2,3,1,2,2,1,3,2,1,2,2,2,3,
    3,0,3,2,2,0,0,1,1,0,1,0,1,2,1,2,3,2,0,1,
    2,2,3,2,3,3,2,0,2,3,3,2,1,2,1,2,0,2,1,0,
    0,2,0,0,3,2,2,1,0,2,3,1,1,2,1,3,2,0,1,1,
    2,1,0,0,2,3,3,3,2,2,0,0,2,1,2,3,2,0,1,3,
    2,0,2,2,0,0,0,1,3,0,3,2,3,3,2,0,2,1,1,2,
    0,0
};

int main(int argc, char *argv[])
{
    const char *call  = (argc > 1) ? argv[1] : "VK2AAX";
    const char *grid  = (argc > 2) ? argv[2] : "QF56";
    int         power = (argc > 3) ? atoi(argv[3]) : 37;

    printf("Encoding: %s %s %d dBm\n\n", call, grid, power);

    uint8_t symbols[WSPR_SYMBOLS];
    if (encode_wspr(call, grid, power, symbols) != 0) {
        fprintf(stderr, "encode_wspr() failed\n");
        return 1;
    }

    /* Print all 162 symbols */
    printf("Symbols (%d total):\n", WSPR_SYMBOLS);
    for (int i = 0; i < WSPR_SYMBOLS; i++) {
        printf("%d", symbols[i]);
        if ((i + 1) % 20 == 0) printf("\n");
        else printf(" ");
    }
    printf("\n");

    /* Symbol distribution */
    int counts[4] = {0};
    for (int i = 0; i < WSPR_SYMBOLS; i++) counts[symbols[i]]++;
    printf("\nSymbol distribution:\n");
    for (int i = 0; i < 4; i++)
        printf("  tone%d = %d\n", i, counts[i]);

    /* Verify against known-good output if using default VK2AAX/QF56/37 */
    if (argc == 1 || (argc == 4 &&
        strcmp(call,"VK2AAX")==0 &&
        strcmp(grid,"QF56")==0 &&
        power == 37))
    {
        int ok = 1;
        for (int i = 0; i < WSPR_SYMBOLS; i++) {
            if (symbols[i] != KNOWN_VK2AAX_QF56_37[i]) {
                printf("\nMISMATCH at symbol %d: got %d expected %d\n",
                       i, symbols[i], KNOWN_VK2AAX_QF56_37[i]);
                ok = 0;
            }
        }
        if (ok)
            printf("\n✓ All 162 symbols match known-good output for VK2AAX QF56 37\n");
        else
            printf("\n✗ Symbol mismatch — check encoding logic\n");
    }

    return 0;
}
#endif /* STANDALONE_TEST */