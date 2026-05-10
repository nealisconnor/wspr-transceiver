/*
 * wspr_wav_gen.c — Generate a WSPR test WAV file
 *
 * Creates a 114-second WAV file with WSPR 4-FSK tones at ~1500 Hz.
 * Used to test the decoder (k9an-wsprd) without hardware.
 *
 * Build:  gcc -o wspr_wav_gen wspr_wav_gen.c wspr_encode.c -lm
 * Usage:  ./wspr_wav_gen VK2AAX QF56 30
 * Test:   ./k9an-wsprd -f 7.0386 wspr_test.wav
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* From wspr_encode.c */
extern void encode_wspr(const char *callsign, const char *grid,
                        int power, unsigned char symbols[162]);

#define SAMPLE_RATE    12000
#define SYMBOL_SAMPLES 8192
#define NUM_SYMBOLS    162
#define AUDIO_CENTER   1500.0
#define TONE_SPACING   (12000.0 / 8192.0)
#define TOTAL_SAMPLES  (114 * SAMPLE_RATE)   /* 1,368,000 */

/* Write a 16-bit mono PCM WAV header */
static void write_wav_header(FILE *fp, uint32_t num_samples)
{
    uint32_t data_size = num_samples * 2;  /* 16-bit = 2 bytes per sample */
    uint32_t file_size = 36 + data_size;
    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t byte_rate = SAMPLE_RATE * channels * bits / 8;
    uint16_t block_align = channels * bits / 8;

    fwrite("RIFF", 1, 4, fp);
    fwrite(&file_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    uint16_t audio_fmt = 1;  /* PCM */
    fwrite(&audio_fmt, 2, 1, fp);
    fwrite(&channels, 2, 1, fp);
    uint32_t sr = SAMPLE_RATE;
    fwrite(&sr, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);
}

int main(int argc, char *argv[])
{
    const char *call  = (argc > 1) ? argv[1] : "VK2AAX";
    const char *grid  = (argc > 2) ? argv[2] : "QF56";
    int         power = (argc > 3) ? atoi(argv[3]) : 3;
    const char *outfile = "wspr_test.wav";

    unsigned char symbols[162];
    double tone_freqs[4];
    int i;

    /* Compute the 4 audio tone frequencies */
    for (i = 0; i < 4; i++)
        tone_freqs[i] = AUDIO_CENTER + i * TONE_SPACING;

    /* Encode the WSPR message */
    encode_wspr(call, grid, power, symbols);

    printf("Generating WSPR WAV: %s %s %d dBm\n", call, grid, power);
    printf("Tones: %.2f  %.2f  %.2f  %.2f Hz\n",
           tone_freqs[0], tone_freqs[1], tone_freqs[2], tone_freqs[3]);

    /* Open output file */
    FILE *fp = fopen(outfile, "wb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s for writing\n", outfile);
        return 1;
    }

    /* Write WAV header */
    write_wav_header(fp, TOTAL_SAMPLES);

    /* Generate audio: 162 symbols of 4-FSK, then silence */
    double phase = 0.0;
    int total_written = 0;

    for (i = 0; i < NUM_SYMBOLS; i++) {
        double freq = tone_freqs[symbols[i]];
        int s;
        for (s = 0; s < SYMBOL_SAMPLES; s++) {
            double sample = 0.5 * sin(phase);
            int16_t pcm = (int16_t)(sample * 32767);
            fwrite(&pcm, 2, 1, fp);
            phase += 2.0 * M_PI * freq / SAMPLE_RATE;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            total_written++;
        }
    }

    /* Pad with silence to 114 seconds */
    int16_t zero = 0;
    while (total_written < TOTAL_SAMPLES) {
        fwrite(&zero, 2, 1, fp);
        total_written++;
    }

    fclose(fp);
    printf("Written %d samples to %s (%.1f sec)\n",
           total_written, outfile, (double)total_written / SAMPLE_RATE);

    return 0;
}
