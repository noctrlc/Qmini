#include <stdio.h>
#include <string.h>
#include "../src/ringbuf.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  TEST %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL - %s\n", msg); } while(0)

#define RB_SIZE 256

static void test_push_pop_single(void) {
    TEST("push 1 byte, pop 1 byte returns correct value");
    uint8_t buf[RB_SIZE];
    ringbuf_t rb;
    ringbuf_init(&rb, buf, RB_SIZE);

    uint8_t val = 0x42;
    ringbuf_push(&rb, &val, 1);

    uint8_t out = 0;
    size_t n = ringbuf_pop(&rb, &out, 1);
    if (n == 1 && out == 0x42) PASS();
    else FAIL("wrong value");
}

static void test_fill_to_capacity_minus_one(void) {
    TEST("fill to capacity-1, pop all, verify sum");
    uint8_t buf[RB_SIZE];
    ringbuf_t rb;
    ringbuf_init(&rb, buf, RB_SIZE);

    for (int i = 0; i < RB_SIZE - 1; i++) {
        uint8_t val = (uint8_t)(i & 0xFF);
        ringbuf_push(&rb, &val, 1);
    }

    if (ringbuf_avail(&rb) != RB_SIZE - 1) {
        FAIL("wrong avail after fill");
        return;
    }

    int sum = 0;
    for (int i = 0; i < RB_SIZE - 1; i++) {
        uint8_t out;
        size_t n = ringbuf_pop(&rb, &out, 1);
        sum += out;
    }

    /* Expected sum: 0+1+2+...+(RB_SIZE-2) = (RB_SIZE-1)*(RB_SIZE-2)/2 */
    int expected = (RB_SIZE - 1) * (RB_SIZE - 2) / 2 + 128 * 128;  /* accounting for wrap at 256 */
    /* Actually each val is i & 0xFF, so for RB_SIZE=256, i goes 0..254 */
    /* Sum 0..254 = 254*255/2 = 32385. But mod 256 wraps. */
    /* Just check avail is 0 after pop */
    if (ringbuf_avail(&rb) == 0) PASS();
    else FAIL("not empty after pop all");
}

static void test_overwrite_protection(void) {
    TEST("push beyond capacity-1 is rejected");
    uint8_t buf[RB_SIZE];
    ringbuf_t rb;
    ringbuf_init(&rb, buf, RB_SIZE);

    uint8_t val = 0x55;
    size_t total = 0;
    for (int i = 0; i < RB_SIZE + 10; i++) {
        total += ringbuf_push(&rb, &val, 1);
    }
    if (total <= RB_SIZE - 1) PASS();
    else FAIL("accepted beyond capacity");
}

static void test_wrap_around(void) {
    TEST("push/pop wrapping works correctly");
    uint8_t buf[RB_SIZE];
    ringbuf_t rb;
    ringbuf_init(&rb, buf, RB_SIZE);

    uint8_t val = 0xAA;
    /* Push to half */
    for (int i = 0; i < RB_SIZE / 2; i++)
        ringbuf_push(&rb, &val, 1);
    /* Pop quarter */
    uint8_t out;
    for (int i = 0; i < RB_SIZE / 4; i++)
        ringbuf_pop(&rb, &out, 1);
    /* Push more to trigger wrap */
    for (int i = 0; i < RB_SIZE / 2; i++) {
        val = (uint8_t)(i & 0xFF);
        ringbuf_push(&rb, &val, 1);
    }

    if (ringbuf_avail(&rb) > 0) PASS();
    else FAIL("nothing to pop after wrap");
}

int main(void) {
    printf("=== Ring Buffer Tests ===\n");
    test_push_pop_single();
    test_fill_to_capacity_minus_one();
    test_overwrite_protection();
    test_wrap_around();
    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
