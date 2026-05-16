#include <stdio.h>
#include <windows.h>
#include "../src/congestion.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  TEST %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL - %s\n", msg); } while(0)

static void test_init(void) {
    TEST("peer_cc_init starts at max bitrate");
    peer_cc_t cc;
    peer_cc_init(&cc);
    if (cc.current_bitrate == CC_BITRATE_MAX) PASS();
    else FAIL("not at max");
}

static void test_stays_max_on_zero_loss(void) {
    TEST("0% loss sustained keeps bitrate at max");
    peer_cc_t cc;
    peer_cc_init(&cc);

    for (int interval = 0; interval < 5; interval++) {
        /* Simulate 1 second of perfect transmission */
        for (int i = 0; i < 50; i++) {
            peer_cc_update_sent(&cc);
            peer_cc_update_acked(&cc, 0);
        }
        /* Force check: advance time + ensure enough packets */
        cc.last_loss_check = 0;  /* force stale */
        int br = peer_cc_get_bitrate(&cc);
        if (br != CC_BITRATE_MAX) {
            FAIL("bitrate dropped on 0% loss");
            return;
        }
    }
    PASS();
}

static void test_drops_on_high_loss(void) {
    TEST(">10% loss sustained drops bitrate");
    peer_cc_t cc;
    peer_cc_init(&cc);

    /* 80% ack rate = 20% loss */
    for (int interval = 0; interval < 10; interval++) {
        for (int i = 0; i < 50; i++) {
            peer_cc_update_sent(&cc);
            if (i % 5 != 0) peer_cc_update_acked(&cc, 0);
            /* every 5th packet lost */
        }
        cc.last_loss_check = 0;
        peer_cc_get_bitrate(&cc);
    }
    /* After 10 intervals of 20% loss, should have dropped */
    if (cc.current_bitrate < CC_BITRATE_MAX) PASS();
    else FAIL("bitrate did not drop on loss");
}

static void test_smoothed_loss_value(void) {
    TEST("smoothed_loss initializes to 0");
    peer_cc_t cc;
    peer_cc_init(&cc);
    if (cc.smoothed_loss == 0.0f) PASS();
    else FAIL("non-zero initial smoothed_loss");
}

int main(void) {
    printf("=== Congestion Control Tests ===\n");
    test_init();
    test_stays_max_on_zero_loss();
    test_drops_on_high_loss();
    test_smoothed_loss_value();
    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
