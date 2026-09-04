/*
 * musl-linked EL0 programs on ARM_OS keep musl's own crt1/crt0 (which
 * runs __libc_start_main -> __init_libc -> __init_tls / stack canary /
 * .init_array -> main). The only problem is that musl's static-PIE
 * `_start` does `adrp x0, _DYNAMIC` for its self-relocation check, and
 * with the image linked at 64 GiB that ADRP is out of its +/-4 GiB range
 * when `_DYNAMIC` is the usual undefined-weak (== 0).
 *
 * Define `_DYNAMIC` ourselves as a single DT_NULL entry in an allocated
 * section: the ADRP now resolves to an in-image address (in range), and
 * rcrt1's `for (d = _DYNAMIC; d->d_tag; d++)` loop sees d_tag == 0 and
 * does nothing. Static, non-PIE: no real relocations to apply.
 */
__asm__(
    ".pushsection .data.rel.ro,\"aw\",@progbits\n"
    ".balign 8\n"
    ".globl _DYNAMIC\n"
    ".hidden _DYNAMIC\n"
    "_DYNAMIC:\n"
    ".quad 0\n"
    ".quad 0\n"
    ".popsection\n");
