/* Démarrage / arrêt d'un module PRX (DT_INIT / DT_FINI) pour nos bibliothèques glissées dans les jeux.
 * Portage en C de crates/ampr-sys/src/crt.rs d'ampr-emu-ng v0.4.1 (quer3q, GPL-3.0-or-later) : mêmes sections, mêmes
 * symboles, même ordre (préinit, constructeurs, module_start ; module_stop, __cxa_finalize, destructeurs). */
#include <stddef.h>
#include <stdint.h>

typedef void (*crt_fn)(void);
typedef int (*module_fn)(size_t, const void*);

__asm__(
    ".pushsection .sceversion,\"\",@progbits\n"
    ".byte 0x00,0x00,0x16,0x00,0x08,0x63,0x72,0x74,0x69,0x3a,0x02,0x00,0x00\n"
    ".byte 0x09,0x00,0x00,0x00,0x01,0x02,0x00,0x00,0x09,0x00,0x00,0x00,0x01\n"
    ".byte 0x00,0x00,0x1b,0x00,0x08,0x63,0x72,0x74,0x62,0x65,0x67,0x69,0x6e,0x53,0x3a,0x02\n"
    ".byte 0x00,0x00,0x09,0x00,0x00,0x00,0x01,0x02,0x00,0x00,0x09,0x00,0x00,0x00,0x01\n"
    ".byte 0x00,0x00,0x19,0x00,0x08,0x63,0x72,0x74,0x65,0x6e,0x64,0x53,0x3a,0x02,0x00,0x00\n"
    ".byte 0x09,0x00,0x00,0x00,0x01,0x02,0x00,0x00,0x09,0x00,0x00,0x00,0x01\n"
    ".byte 0x00,0x00,0x16,0x00,0x08,0x63,0x72,0x74,0x6e,0x3a,0x02,0x00,0x00\n"
    ".byte 0x09,0x00,0x00,0x00,0x01,0x02,0x00,0x00,0x09,0x00,0x00,0x00,0x01\n"
    ".popsection\n"
    ".pushsection .data.__dso_handle,\"aw\",@progbits\n"
    ".p2align 3\n.globl __dso_handle\n.hidden __dso_handle\n__dso_handle: .quad __dso_handle\n"
    ".popsection\n"
    ".pushsection .data.sceLibcInternal,\"aw\",@progbits\n"
    ".p2align 3\n.globl pbc_crt_libc_anchor\n.hidden pbc_crt_libc_anchor\npbc_crt_libc_anchor: .quad Need_sceLibcInternal\n"
    ".popsection\n"
    ".pushsection .ctors,\"aw\",@progbits\n.p2align 3\n.quad -1\n.quad 0\n.popsection\n"
    ".pushsection .dtors,\"aw\",@progbits\n.p2align 3\n.quad -1\n.quad 0\n.popsection\n"
    ".pushsection .sce_module_param,\"a\",@progbits\n"
    ".p2align 3\n.globl _sceModuleParam\n.hidden _sceModuleParam\n_sceModuleParam:\n"
    ".byte 0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00\n"
    ".byte 0xbf,0xf4,0x13,0x3c,0x03,0x00,0x00,0x00\n"
    ".byte 0x01,0x00,0x05,0x08,0x09,0x00,0x00,0x02\n"
    ".byte 0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00\n"
    ".popsection\n"
    ".weak __cxa_finalize\n"
    ".pushsection .data.rel.ro.pbc_crt,\"aw\",@progbits\n"
    ".p2align 3\n.globl pbc_crt_cxa_finalize\n.hidden pbc_crt_cxa_finalize\npbc_crt_cxa_finalize: .quad __cxa_finalize\n"
    ".popsection\n");

/* Ancre de libSceLibcInternal (défini ici, comme ampr-emu-ng dans lib.rs). */
int Need_sceLibcInternal = 0;

extern void (*const pbc_crt_cxa_finalize)(void*);
extern const int* const pbc_crt_libc_anchor;
extern void* __dso_handle;
extern crt_fn __start__Zpreinit_array[], __stop__Zpreinit_array[];
extern crt_fn __CTOR_LIST__[], __CTOR_END__[], __DTOR_LIST__[], __DTOR_END__[];

/* Le module peut définir les siennes ; sinon, rien à faire. */
__attribute__((weak)) int module_start(size_t args, const void* argp) { (void)args; (void)argp; return 0; }
__attribute__((weak)) int module_stop(size_t args, const void* argp) { (void)args; (void)argp; return 0; }

static volatile int fini_done;

__attribute__((section(".init"), visibility("hidden"))) int _init(size_t args, const void* argp, module_fn over) {
    (void)*(const int* const volatile*)&pbc_crt_libc_anchor;
    for (crt_fn* p = __start__Zpreinit_array; p < __stop__Zpreinit_array; ++p) if (*p) (*p)();
    for (crt_fn* p = __CTOR_END__; p > __CTOR_LIST__;) {
        --p;
        const uintptr_t a = (uintptr_t)*p;
        if (a != 0 && a != UINTPTR_MAX) (*p)();
    }
    return over ? over(args, argp) : module_start(args, argp);
}

__attribute__((section(".fini"), visibility("hidden"))) int _fini(size_t args, const void* argp, module_fn over) {
    if (fini_done) return 0;
    const int r = over ? over(args, argp) : module_stop(args, argp);
    if (pbc_crt_cxa_finalize) pbc_crt_cxa_finalize(__dso_handle);
    for (crt_fn* p = __DTOR_LIST__ + 1; p < __DTOR_END__ && *p; ++p) (*p)();
    fini_done = 1;
    return r;
}
