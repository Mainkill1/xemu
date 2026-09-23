#include "qemu/osdep.h"

#include <math.h>

#include "hw/xbox/mcpx/apu/vp/voice_math.h"

static uint32_t float_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float float_from_bits(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float reference_clamp(float value)
{
    return (float)fmin(fmax((double)value, -1.0), 1.0);
}

static void assert_same_clamp(float value)
{
    float expected = reference_clamp(value);
    float actual = mcpx_apu_clamp_sample(value);

    g_assert_cmphex(float_bits(actual), ==, float_bits(expected));
}

static void test_special_values(void)
{
    const uint32_t values[] = {
        0xff800000, /* -infinity */
        0xff7fffff, /* lowest finite */
        0xbf800001, /* just below -1 */
        0xbf800000, /* -1 */
        0xbf7fffff, /* just above -1 */
        0x80000001, /* negative subnormal */
        0x80000000, /* negative zero */
        0x00000000, /* positive zero */
        0x00000001, /* positive subnormal */
        0x3f7fffff, /* just below 1 */
        0x3f800000, /* 1 */
        0x3f800001, /* just above 1 */
        0x7f7fffff, /* highest finite */
        0x7f800000, /* infinity */
        0x7f800001, /* positive signaling NaN */
        0x7fc00000, /* positive quiet NaN */
        0xff800001, /* negative signaling NaN */
        0xffc00000, /* negative quiet NaN */
    };

    for (size_t i = 0; i < G_N_ELEMENTS(values); i++) {
        assert_same_clamp(float_from_bits(values[i]));
    }
}

static void test_representative_bit_patterns(void)
{
    uint32_t bits = 0x12345678;

    for (unsigned int i = 0; i < (1U << 20); i++) {
        bits = bits * 1664525U + 1013904223U;
        assert_same_clamp(float_from_bits(bits));
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/mcpx-apu/voice-math/clamp-special-values",
                    test_special_values);
    g_test_add_func("/mcpx-apu/voice-math/clamp-representative-patterns",
                    test_representative_bit_patterns);

    return g_test_run();
}
