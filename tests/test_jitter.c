#include <stdio.h>
#include <string.h>
#include "../src/jitter_buffer.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  TEST %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL - %s\n", msg); } while(0)

static void test_empty_pop(void) {
    TEST("empty jb returns 0 on pop");
    jitter_buffer_t jb;
    jitter_buffer_init(&jb);
    uint8_t buf[400];
    int sz = jitter_buffer_pop(&jb, buf, NULL);
    if (sz == 0) PASS(); else FAIL("expected 0");
    jitter_buffer_destroy(&jb);
}

static void test_push_pop_ordering(void) {
    TEST("push 5 pop 5 gives correct sizes");
    jitter_buffer_t jb;
    jitter_buffer_init(&jb);
    jb.target_level = 0;
    jb.min_target = 0;
    jb.max_target = 0;  /* fully disable adaptive target override */

    uint8_t data[400];
    for (int i = 0; i < 5; i++) {
        memset(data, (char)i, i + 20);
        jitter_buffer_push(&jb, data, i + 20, (uint16_t)i);
    }

    uint8_t out[400];
    uint16_t seq;
    for (int i = 0; i < 5; i++) {
        int sz = jitter_buffer_pop(&jb, out, &seq);
        if (sz != i + 20) { FAIL("wrong size"); jitter_buffer_destroy(&jb); return; }
    }
    PASS();
    jitter_buffer_destroy(&jb);
}

static void test_capacity_limit(void) {
    TEST("push beyond capacity is rejected");
    jitter_buffer_t jb;
    jitter_buffer_init(&jb);
    jb.target_level = 0;
    jb.min_target = 0;  /* disable adaptive override */

    uint8_t data[50];
    memset(data, 0xAA, sizeof(data));
    int pushed = 0;
    for (int i = 0; i < JB_CAPACITY + 10; i++) {
        jitter_buffer_push(&jb, data, sizeof(data), (uint16_t)i);
        pushed++;
    }
    /* Only JB_CAPACITY should be stored */
    if (jb.count <= JB_CAPACITY) PASS();
    else FAIL("over capacity");
    jitter_buffer_destroy(&jb);
}

static void test_target_level(void) {
    TEST("pop returns 0 when count <= target_level");
    jitter_buffer_t jb;
    jitter_buffer_init(&jb);
    jb.target_level = 4;
    jb.min_target = 4;   /* prevent adaptive override */
    jb.max_target = 4;
    uint8_t data[50];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < 4; i++)
        jitter_buffer_push(&jb, data, sizeof(data), (uint16_t)i);

    uint8_t out[400];
    int sz = jitter_buffer_pop(&jb, out, NULL);
    if (sz == 0) PASS(); else FAIL("should not pop yet");
    jitter_buffer_destroy(&jb);
}

static void test_destroy_cleanup(void) {
    TEST("destroy then reuse works");
    jitter_buffer_t jb;
    jitter_buffer_init(&jb);
    jitter_buffer_destroy(&jb);

    jitter_buffer_init(&jb);
    uint8_t data[20];
    jitter_buffer_push(&jb, data, sizeof(data), 0);
    jitter_buffer_destroy(&jb);
    PASS();
}

int main(void) {
    printf("=== Jitter Buffer Tests ===\n");
    test_empty_pop();
    test_push_pop_ordering();
    test_capacity_limit();
    test_target_level();
    test_destroy_cleanup();
    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
