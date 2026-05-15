/*
 * wspr_transmit.c — WSPR Transmitter for ELEC3607 WSPR-SDR
 *
 * Drives Si5351A CLK2 output via I²C to generate a WSPR-encoded
 * 4-FSK transmission on the 40m band (~7.040100 MHz).
 *
 * Usage (just like in the wsprsharp):
 *   ./wspr_transmit <callsign> <grid> <power_dBm> [--now]
 *
 * Example:
 *   ./wspr_transmit VK2AAX QF56 3
 *   ./wspr_transmit VK2AAX QF56 3 --now        # skip even-minute wait
 *
 * Hardware:
 *   - Linux SBC with I²C bus 3
 *   - Si5351A at address 0x60
 *   - PLL_A  → CLK0/CLK1 (receiver LO, undisturbed)
 *   - PLL_B  → CLK2 (transmitter, configured here)
 *
 * ELEC3607 — WSPR Transceiver Project
 */
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// From wspr_encode.c
#define WSPR_SYMBOL_COUNT   162
#define WSPR_TONE_SPACING   (12000.0 / 8192.0)   /* ~1.4648 Hz */
#define WSPR_SYMBOL_US      682667                /* 8192/12000 * 1e6 */
#define WSPR_NUM_TONES      4

/* Provided by wspr_encode.c — link with: gcc ... wspr_encode.c */
extern int encode_wspr(const char *callsign, const char *grid,
                       int power, uint8_t symbols[WSPR_SYMBOL_COUNT]);

/* ================================================================== */
/* >>>>>>>>>> CHECK HERE: Configuration Constants <<<<<<<<<<            */
/* Si5351 address, crystal frequency, WSPR base frequency              */
/* Verify these match your board hardware                              */
/* ================================================================== */

#define I2C_DEVICE      "/dev/i2c-3"
#define SI5351_ADDR     0x60

/* Si5351A crystal frequency (Hz) */
#define XTAL_FREQ       27000000.0

/* WSPR dial frequency for 40m band (Hz) */
/* The actual TX freq is within the 200 Hz sub-band above this */
#define WSPR_BASE_FREQ  7040100.0

/* MultiSynth2 integer divider for CLK2 */
#define MS2_DIVIDER     126

/* PLL_B fractional divider denominator.
 * Larger = finer resolution. Max is 1,048,575 (20-bit).
 * 1,000,000 gives ~0.027 Hz resolution — more than enough. */
#define PLLB_DENOM      1000000UL

/* ================================================================== */
/* Si5351A Register Addresses (from AN619 datasheet)                   */
/* Same registers used in Lab 3 (si5351prog.c) for CLK0/CLK1,         */
/* extended here with PLL_B and MS2 registers for CLK2 TX              */
/* ================================================================== */

/* Output enable control */
#define REG_OEB_CTRL        3

/* CLK2 control register */
#define REG_CLK2_CTRL       18

/* PLL_B feedback multisynth (MSNB) registers — used for CLK2 tone frequencies */
#define REG_MSNB_P3_HI      34      /* 0x22 */
#define REG_MSNB_P3_LO      35      /* 0x23 */
#define REG_MSNB_P1_HI      36      /* 0x24 */
#define REG_MSNB_P1_MID     37      /* 0x25 */
#define REG_MSNB_P1_LO      38      /* 0x26 */
#define REG_MSNB_P23_HI     39      /* 0x27 */
#define REG_MSNB_P2_MID     40      /* 0x28 */
#define REG_MSNB_P2_LO      41      /* 0x29 */

/* MultiSynth2 registers (MS2, for CLK2) */
#define REG_MS2_P3_HI       58      /* 0x3A */
#define REG_MS2_P3_LO       59      /* 0x3B */
#define REG_MS2_P1_HI       60      /* 0x3C */
#define REG_MS2_P1_MID      61      /* 0x3D */
#define REG_MS2_P1_LO       62      /* 0x3E */
#define REG_MS2_P23_HI      63      /* 0x3F */
#define REG_MS2_P2_MID      64      /* 0x40 */
#define REG_MS2_P2_LO       65      /* 0x41 */

/* PLL soft reset */
#define REG_PLL_RESET       177

/* ================================================================== */
/* PLL Parameter Structure — new for transmitter (not in labs)          */
/* ================================================================== */

/*
 * Pre-computed Si5351 PLL register values for one WSPR tone.
 * PLL output = XTAL_FREQ × (a + b/c)
 * CLK2 output = PLL_B / MS2_DIVIDER
 */
typedef struct {
    unsigned long p1;       /* MSNB_P1 register value */
    unsigned long p2;       /* MSNB_P2 register value */
    unsigned long p3;       /* MSNB_P3 register value (= PLLB_DENOM) */
    double actual_freq;     /* Actual output frequency achieved */
} tone_params_t;

/* ================================================================== */
/* I²C Interface — same as Lab 3 (si5351prog.c)                        */
/* ================================================================== */

static int i2c_fd = -1;

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void i2c_open(void)
{
    i2c_fd = open(I2C_DEVICE, O_RDWR);
    if (i2c_fd < 0)
        die("open " I2C_DEVICE);

    if (ioctl(i2c_fd, I2C_SLAVE, SI5351_ADDR) < 0)
        die("ioctl I2C_SLAVE");
}

static void i2c_close(void)
{
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
}

static void i2c_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    if (write(i2c_fd, buf, 2) != 2)
        die("i2c write");
}

/* ================================================================== */
/* Si5351A Receiver Init — handled by si5351_prog (Lab 3)              */
/*                                                                     */
/* Run ./si5351_prog FIRST to initialize CLK0/CLK1 using the           */
/* ClockBuilder Pro header file (Si5351A-RevB-Registers.h).            */
/* This program only configures CLK2 for transmission on top of that.  */
/* ================================================================== */

/* ================================================================== */
/* CLK2 Transmitter Configuration — NEW for this assignment            */
/* Extends Lab 3 Si5351 control to use PLL_B + MS2 for CLK2 output     */
/* ================================================================== */

/*
 * >>>>>>>>>> CHECK HERE: PLL_B frequency calculation <<<<<<<<<<
 *
 * Compute PLL_B register parameters for a target output frequency.
 * This is the core math that converts a frequency (Hz) into Si5351 register values.
 *
 * CLK2 frequency = PLL_B_VCO / MS2_DIVIDER
 * PLL_B_VCO = XTAL_FREQ x (a + b/c)
 *
 * So: a + b/c = (f_target x MS2_DIVIDER) / XTAL_FREQ
 *
 * The P1, P2, P3 register values are computed per the AN619 datasheet formula.
 * Verify: do the 4 tone frequencies look correct in the output?
 */
static void compute_tone_params(double freq_hz, tone_params_t *tp)
{
    double f_vco, m_pllb, frac;
    unsigned long a, b, c;
    unsigned long p1, p2, p3;
    double actual_vco, actual_freq;

    c = PLLB_DENOM;

    /* VCO frequency needed */
    f_vco = freq_hz * MS2_DIVIDER;

    /* PLL feedback divider M = a + b/c */
    m_pllb = f_vco / XTAL_FREQ;
    a = (unsigned long)m_pllb;
    frac = m_pllb - (double)a;
    b = (unsigned long)round(frac * c);

    /* Clamp b to valid range */
    if (b >= c) {
        b = c - 1;
    }

    /* Compute register values per AN619 */
    p1 = 128 * a + (128 * b / c) - 512;
    p2 = 128 * b - c * (128 * b / c);
    p3 = c;

    /* Compute actual achieved frequency for verification */
    actual_vco = XTAL_FREQ * ((double)a + (double)b / (double)c);
    actual_freq = actual_vco / MS2_DIVIDER;

    tp->p1 = p1;
    tp->p2 = p2;
    tp->p3 = p3;
    tp->actual_freq = actual_freq;
}

/*
 * Write PLL_B (MSNB) register values to the Si5351A.
 * This updates the VCO frequency for CLK2.
 */
static void si5351_set_pllb(const tone_params_t *tp)
{
    unsigned long p1 = tp->p1;
    unsigned long p2 = tp->p2;
    unsigned long p3 = tp->p3;

    i2c_write_reg(REG_MSNB_P3_HI,  (p3 >> 8) & 0xFF);
    i2c_write_reg(REG_MSNB_P3_LO,  p3 & 0xFF);
    i2c_write_reg(REG_MSNB_P1_HI,  (p1 >> 16) & 0x03);
    i2c_write_reg(REG_MSNB_P1_MID, (p1 >> 8) & 0xFF);
    i2c_write_reg(REG_MSNB_P1_LO,  p1 & 0xFF);
    i2c_write_reg(REG_MSNB_P23_HI, ((p3 >> 12) & 0xF0) |
                                     ((p2 >> 16) & 0x0F));
    i2c_write_reg(REG_MSNB_P2_MID, (p2 >> 8) & 0xFF);
    i2c_write_reg(REG_MSNB_P2_LO,  p2 & 0xFF);
}

/*
 * Configure MultiSynth2 as a fixed integer divider (N = MS2_DIVIDER).
 * P1 = 128*N - 512, P2 = 0, P3 = 1
 */
static void si5351_setup_ms2(void)
{
    unsigned long p1 = 128 * MS2_DIVIDER - 512;

    printf("Configuring MS2: integer divider N=%d (P1=%lu)\n",
           MS2_DIVIDER, p1);

    i2c_write_reg(REG_MS2_P3_HI,  0x00);       /* P3 = 1 (high byte) */
    i2c_write_reg(REG_MS2_P3_LO,  0x01);       /* P3 = 1 (low byte)  */
    i2c_write_reg(REG_MS2_P1_HI,  (p1 >> 16) & 0x03);
    i2c_write_reg(REG_MS2_P1_MID, (p1 >> 8) & 0xFF);
    i2c_write_reg(REG_MS2_P1_LO,  p1 & 0xFF);
    i2c_write_reg(REG_MS2_P23_HI, 0x00);       /* P3[19:16]=0, P2[19:16]=0 */
    i2c_write_reg(REG_MS2_P2_MID, 0x00);       /* P2 = 0 */
    i2c_write_reg(REG_MS2_P2_LO,  0x00);       /* P2 = 0 */
}

/*
 * >>>>>>>>>> CHECK HERE: CLK2 control register bit pattern <<<<<<<<<<
 *
 *   Bit 7: CLK2_PDN  = 0 (powered up)
 *   Bit 6: MS2_INT   = 1 (integer mode for MS2)
 *   Bit 5: MS2_SRC   = 1 (PLL_B)
 *   Bit 4: CLK2_INV  = 0
 *   Bit 3:2: CLK2_SRC = 11 (MultiSynth2)
 *   Bit 1:0: CLK2_IDRV = 01 (4 mA drive for loopback)
 *
 *   = 0b0110_1101 = 0x6D
 *
 * Verify: is 0x6D the correct bit pattern for these settings?
 * (see Si5351A datasheet Table 3, register 18 for CLK2)
 */
static void si5351_enable_clk2(void)
{
    printf("Enabling CLK2 (PLL_B, MS2, 4 mA drive)\n");
    i2c_write_reg(REG_CLK2_CTRL, 0x6D);

    /* Reset PLL_B to lock cleanly */
    i2c_write_reg(REG_PLL_RESET, 0x80);    /* Reset PLL_B */
}

/*
 * Disable CLK2 output (power down).
 */
static void si5351_disable_clk2(void)
{
    printf("Disabling CLK2\n");
    i2c_write_reg(REG_CLK2_CTRL, 0x8C);    /* CLK2_PDN=1, powered down */
}

/* ================================================================== */
/* WSPR Timing — based on Lab 5/6 (wsprwait script timing logic)       */
/* ================================================================== */

/*
 * Wait until the start of the next even UTC minute (WSPR timing slot).
 * WSPR transmissions must begin at second 1 of an even minute.
 */
static void wait_for_even_minute(void)
{
    time_t now;
    struct tm *utc;
    int sec_to_wait;

    printf("Waiting for even UTC minute...\n");

    for (;;) {
        now = time(NULL);
        utc = gmtime(&now);

        /* Check if we're at second 0–1 of an even minute */
        if ((utc->tm_min % 2 == 0) && (utc->tm_sec <= 1)) {
            printf("WSPR slot start: %02d:%02d:%02d UTC\n",
                   utc->tm_hour, utc->tm_min, utc->tm_sec);
            return;
        }

        /* Calculate seconds until next even minute boundary */
        if (utc->tm_min % 2 == 0) {
            /* Currently even minute but past second 1 — wait for next */
            sec_to_wait = 120 - utc->tm_sec;
        } else {
            /* Odd minute — wait until next even minute */
            sec_to_wait = 60 - utc->tm_sec;
        }

        if (sec_to_wait > 10)
            printf("  Waiting %d seconds...\n", sec_to_wait);

        /* Sleep in short intervals to stay responsive */
        if (sec_to_wait > 5) {
            struct timespec ts = { sec_to_wait - 2, 0 };
            nanosleep(&ts, NULL);
        } else {
            struct timespec ts = { 0, 100000000 };  /* 100 ms */
            nanosleep(&ts, NULL);
        }
    }
}

/*
 * Precise delay for one WSPR symbol period (682,667 µs).
 * Uses clock_nanosleep for monotonic timing to avoid drift.
 */
static void symbol_delay(void)
{
    struct timespec ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = WSPR_SYMBOL_US * 1000L;   /* Convert µs to ns */
    nanosleep(&ts, NULL);
}

/* ================================================================== */
/* Main Program — ties together encoder (Lab 5/6) + Si5351 (Lab 3)     */
/* ================================================================== */

static void print_usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s <callsign> <grid> <power_dBm> [options]\n"
        "\n"
        "  callsign   Amateur radio callsign (e.g. VK2AAX)\n"
        "  grid       4-char Maidenhead grid locator (e.g. QF56)\n"
        "  power_dBm  TX power in dBm (e.g. 3)\n"
        "\n"
        "Options:\n"
        "  --now      Transmit immediately (don't wait for even minute)\n"
        "\n"
        "Example:\n"
        "  %s VK2AAX QF56 3\n"
        "  %s VK2AAX QF56 3 --now\n",
        progname, progname, progname);
}

int main(int argc, char *argv[])
{
    unsigned char symbols[WSPR_SYMBOL_COUNT];
    tone_params_t tones[WSPR_NUM_TONES];
    int power_dbm;
    int opt_now = 0;
    int i;

    /* ---- Parse arguments ---- */

    if (argc < 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *callsign = argv[1];
    const char *grid     = argv[2];
    power_dbm            = atoi(argv[3]);

    for (i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--now") == 0)
            opt_now = 1;
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    printf("=== WSPR Transmitter ===\n");
    printf("Callsign : %s\n", callsign);
    printf("Grid     : %s\n", grid);
    printf("Power    : %d dBm\n", power_dbm);
    printf("TX Freq  : %.1f Hz (base)\n", WSPR_BASE_FREQ);
    printf("Mode     : %s\n",
           opt_now ? "immediate" : "wait for even minute");
    printf("\n");

    // >>>>>>>>>> CHECK HERE: Encode WSPR message <<<<<<<<<<
    // This calls encode_wspr() from wspr_encode.c

    if (encode_wspr(callsign, grid, power_dbm, symbols) != 0) {
        fprintf(stderr, "Error: encode_wspr() failed. Check inputs.\n");
        return EXIT_FAILURE;
    }

    printf("\nWSPR symbols (162):\n");
    for (i = 0; i < WSPR_SYMBOL_COUNT; i++) {
        printf("%d", symbols[i]);
        if ((i + 1) % 40 == 0) printf("\n");
    }
    if (WSPR_SYMBOL_COUNT % 40 != 0) printf("\n");
    printf("\n");

    // Pre-compute PLL_B parameters for 4 tones

    printf("Tone frequencies (MS2_div=%d, PLL_B denom=%lu):\n",
           MS2_DIVIDER, PLLB_DENOM);


    // >>>>>>>>>> CHECK HERE: tone frequency computation <<<<<<<<<<
    // f_target = 7,040,100 + i x 1.4648 (the 4 WSPR tone frequencies)
    // Are these frequencies within the 40m WSPR sub-band (7,040,000 - 7,040,200 Hz)?
    for (i = 0; i < WSPR_NUM_TONES; i++) {
        double f_target = WSPR_BASE_FREQ + i * WSPR_TONE_SPACING;
        compute_tone_params(f_target, &tones[i]);

        printf("  Tone %d: target=%.3f Hz  actual=%.3f Hz  "
               "error=%+.4f Hz  P1=%lu P2=%lu P3=%lu\n",
               i, f_target, tones[i].actual_freq,
               tones[i].actual_freq - f_target,
               tones[i].p1, tones[i].p2, tones[i].p3);
    }
    printf("\n");



    /* >>>>>>>>>> CHECK HERE: I²C and CLK2 setup <<<<<<<<<< */
    /* NOTE: si5351_prog must be run FIRST to initialize CLK0/CLK1 */
    /* This opens the I2C bus, configures MS2 divider, and enables CLK2 */

    i2c_open();

    /* Configure PLL_B and MultiSynth2 for CLK2 (receiver already set up) */
    si5351_setup_ms2();

    /* Set PLL_B to the first tone frequency initially */
    si5351_set_pllb(&tones[0]);

    /* Enable CLK2 output */
    si5351_enable_clk2();

    /* ---- Step 4: Wait for WSPR timing slot ---- */

    if (!opt_now) {
        wait_for_even_minute();
    } else {
        printf("Immediate mode — transmitting now!\n");
    }

    /* ---- Step 5: Transmit 162 symbols ---- */

    printf("Transmitting %d symbols (%.1f seconds)...\n",
           WSPR_SYMBOL_COUNT,
           WSPR_SYMBOL_COUNT * WSPR_SYMBOL_US / 1e6);

    /*
     * >>>>>>>>>> CHECK HERE: TX loop logic <<<<<<<<<<
     * For each symbol (0-161), look up which tone (0-3) to play,
     * set the Si5351 PLL_B to that tone's frequency, then wait
     * one symbol period (682.667 ms). This is the actual transmission.
     */
    for (i = 0; i < WSPR_SYMBOL_COUNT; i++) {
        int tone = symbols[i];

        /* Update PLL_B frequency for this symbol's tone */
        si5351_set_pllb(&tones[tone]);

        /* Progress indicator every 20 symbols */
        if (i % 20 == 0) {
            printf("  Symbol %3d/%d  tone=%d  freq=%.3f Hz\n",
                   i, WSPR_SYMBOL_COUNT, tone,
                   tones[tone].actual_freq);
        }

        /* Wait one symbol period */
        symbol_delay();
    }

    printf("\nTransmission complete!\n");

    /* Disable and clean up part nothing much here */

    si5351_disable_clk2();
    i2c_close();

    printf("Done.\n");
    return EXIT_SUCCESS;
}
