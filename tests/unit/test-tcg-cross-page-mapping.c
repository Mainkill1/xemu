#include "qemu/osdep.h"

#include "accel/tcg/tb-mapping.h"

static void test_mapping_match(void)
{
    vaddr pc = 0x00123456;
    uintptr_t addend = 0x7f0000000000;

    g_assert_true(tb_code_mapping_matches(pc, pc & TARGET_PAGE_MASK,
                                          addend, addend));
}

static void test_invalid_entry_rejected(void)
{
    vaddr pc = 0x00123456;
    uintptr_t addend = 0x7f0000000000;
    uintptr_t addr_code = (pc & TARGET_PAGE_MASK) | TLB_INVALID_MASK;

    g_assert_false(tb_code_mapping_matches(pc, addr_code, addend, addend));
}

static void test_remapped_entry_rejected(void)
{
    vaddr pc = 0x00123456;
    uintptr_t old_addend = 0x7f0000000000;
    uintptr_t new_addend = 0x7e0000000000;

    g_assert_false(tb_code_mapping_matches(pc, pc & TARGET_PAGE_MASK,
                                           new_addend, old_addend));
}

static void test_other_virtual_page_rejected(void)
{
    vaddr pc = 0x00123456;
    uintptr_t addend = 0x7f0000000000;
    uintptr_t addr_code = (pc & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;

    g_assert_false(tb_code_mapping_matches(pc, addr_code, addend, addend));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tcg/cross-page-mapping/match", test_mapping_match);
    g_test_add_func("/tcg/cross-page-mapping/invalid",
                    test_invalid_entry_rejected);
    g_test_add_func("/tcg/cross-page-mapping/remapped",
                    test_remapped_entry_rejected);
    g_test_add_func("/tcg/cross-page-mapping/other-page",
                    test_other_virtual_page_rejected);
    return g_test_run();
}
