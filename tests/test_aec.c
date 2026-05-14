#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/aec.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %d: %s ... ", tests_run, name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL - %s\n", msg); } while(0)

/* Test 1: aec_create returns non-NULL */
static void test_create(void)
{
    TEST("aec_create returns non-NULL");
    aec_t *aec = aec_create();
    if (aec != NULL) {
        PASS();
    } else {
        FAIL("aec_create returned NULL");
    }
    aec_destroy(aec);
}

/* Test 2: aec_destroy doesn't crash */
static void test_destroy_null(void)
{
    TEST("aec_destroy doesn't crash on NULL");
    aec_destroy(NULL);
    PASS();
}

/* Test 3: No echo reference passes through unchanged */
static void test_no_echo_ref(void)
{
    TEST("no echo reference passes through (far_ref = 0)");
    aec_t *aec = aec_create();
    if (!aec) { FAIL("create failed"); return; }

    short near_in[AEC_FRAME_SIZE];
    short far_ref[AEC_FRAME_SIZE];
    short near_out[AEC_FRAME_SIZE];

    /* Fill near_in with a signal, far_ref with zeros */
    for (int i = 0; i < AEC_FRAME_SIZE; i++) {
        near_in[i] = (short)(1000 * sin(2.0 * 3.14159 * 300.0 * i / AEC_SAMPLE_RATE));
        far_ref[i] = 0;
    }

    aec_process(aec, near_in, far_ref, near_out);

    /* With no far reference, output should be close to input */
    double max_diff = 0;
    for (int i = 0; i < AEC_FRAME_SIZE; i++) {
        double diff = fabs((double)near_out[i] - (double)near_in[i]);
        if (diff > max_diff) max_diff = diff;
    }

    if (max_diff < 50.0) {
        PASS();
    } else {
        FAIL("output diverged from input when far_ref is zero");
    }

    aec_destroy(aec);
}

/* Test 4: Echo is partially cancelled after adaptation */
static void test_echo_cancellation(void)
{
    TEST("echo is partially cancelled after adaptation");
    aec_t *aec = aec_create();
    if (!aec) { FAIL("create failed"); return; }

    short near_in[AEC_FRAME_SIZE];
    short far_ref[AEC_FRAME_SIZE];
    short near_out[AEC_FRAME_SIZE];

    /*
     * Simulate: near_in = original_voice + echo_of_far_ref
     * far_ref is a known signal, echo is far_ref delayed/attenuated
     * After enough adaptation frames, AEC should reduce the echo component.
     */
    double total_error_before = 0;
    double total_error_after = 0;

    /* Run several frames to let the filter adapt */
    for (int frame = 0; frame < 200; frame++) {
        for (int i = 0; i < AEC_FRAME_SIZE; i++) {
            /* Far reference: 500 Hz tone */
            far_ref[i] = (short)(3000 * sin(2.0 * 3.14159 * 500.0 * (frame * AEC_FRAME_SIZE + i) / AEC_SAMPLE_RATE));

            /* Near end: quiet voice + echo (copy of far_ref with some attenuation) */
            short echo = (short)(far_ref[i] * 0.8);
            short voice = (short)(200 * sin(2.0 * 3.14159 * 1000.0 * (frame * AEC_FRAME_SIZE + i) / AEC_SAMPLE_RATE));
            near_in[i] = voice + echo;
        }

        aec_process(aec, near_in, far_ref, near_out);

        /* Measure residual error (how much echo remains) */
        double frame_err = 0;
        for (int i = 0; i < AEC_FRAME_SIZE; i++) {
            frame_err += (double)near_out[i] * (double)near_out[i];
        }

        if (frame < 10) {
            total_error_before += frame_err;
        }
        if (frame >= 180) {
            total_error_after += frame_err;
        }
    }

    /* After adaptation, error should be smaller */
    if (total_error_after < total_error_before * 0.8) {
        PASS();
    } else {
        printf("FAIL - error before=%.0f after=%.0f\n", total_error_before, total_error_after);
    }

    aec_destroy(aec);
}

int main(void)
{
    printf("=== AEC Unit Tests ===\n");

    test_create();
    test_destroy_null();
    test_no_echo_ref();
    test_echo_cancellation();

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
