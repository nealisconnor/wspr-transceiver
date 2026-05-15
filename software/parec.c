#include <stdio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

int parec(short *pabuf, int npoints) {
    int error;

    static const pa_sample_spec ss = {
        .format   = PA_SAMPLE_S16LE,
        .rate     = 12000,
        .channels = 1
    };

    pa_simple *s = pa_simple_new(NULL, "k9an-wsprd", PA_STREAM_RECORD,
                                 NULL, "record", &ss, NULL, NULL, &error);
    if (!s) {
        fprintf(stderr, "pa_simple_new() failed: %s\n", pa_strerror(error));
        return -1;
    }

    /* Read npoints samples, each 2 bytes (S16LE mono) */
    if (pa_simple_read(s, pabuf, npoints * sizeof(short), &error) < 0) {
        fprintf(stderr, "pa_simple_read() failed: %s\n", pa_strerror(error));
        pa_simple_free(s);
        return -1;
    }

    pa_simple_free(s);
    return 0;
}
