#include <string.h>
#include "check.h"
#include "../src/view/frame_text.h"

static FrameText text;

static void test_strings_stay_distinct_until_reset(void)
{
    int a_length, b_length;
    frame_text_reset(&text);
    const char *a = frame_text_format(&text, &a_length, "%s-%d", "one", 1);
    const char *b = frame_text_format(&text, &b_length, "%s", "two");
    CHECK(a_length == 5 && strcmp(a, "one-1") == 0);
    CHECK(b_length == 3 && strcmp(b, "two") == 0);
    frame_text_reset(&text);
    CHECK(frame_text_format(&text, &a_length, "x") == a);
}

static void test_overfilling_yields_empty_strings_inside_the_buffer(void)
{
    char big[1001];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    frame_text_reset(&text);
    for (int i = 0; i < 100; i++) {
        int length = -1;
        const char *out = frame_text_format(&text, &length, "%s", big);
        CHECK(length >= 0 && length <= (int)sizeof big - 1);
        CHECK(out >= text.bytes && out + length < text.bytes + FRAME_TEXT_SIZE);
        CHECK(out[length] == '\0');
        CHECK(text.used <= FRAME_TEXT_SIZE);
    }
    int length = -1;
    const char *out = frame_text_format(&text, &length, "late");
    CHECK(length == 0 && out[0] == '\0' && out < text.bytes + FRAME_TEXT_SIZE);
}

void run_frame_text_tests(void)
{
    test_strings_stay_distinct_until_reset();
    test_overfilling_yields_empty_strings_inside_the_buffer();
}
