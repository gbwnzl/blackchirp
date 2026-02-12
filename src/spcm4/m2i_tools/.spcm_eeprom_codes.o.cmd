savedcmd_/home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o := gcc-13 -Wp,-MMD,/home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/.spcm_eeprom_codes.o.d -nostdinc -I./arch/x86/include -I./arch/x86/include/generated  -I./include -I./arch/x86/include/uapi -I./arch/x86/include/generated/uapi -I./include/uapi -I./include/generated/uapi -include ./include/linux/compiler-version.h -include ./include/linux/kconfig.h -I./ubuntu/include -include ./include/linux/compiler_types.h -D__KERNEL__ -fmacro-prefix-map=./= -std=gnu11 -fshort-wchar -funsigned-char -fno-common -fno-PIE -fno-strict-aliasing -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx -fcf-protection=none -m64 -falign-jumps=1 -falign-loops=1 -mno-80387 -mno-fp-ret-in-387 -mpreferred-stack-boundary=3 -mskip-rax-setup -mtune=generic -mno-red-zone -mcmodel=kernel -Wno-sign-compare -fno-asynchronous-unwind-tables -mindirect-branch=thunk-extern -mindirect-branch-register -mindirect-branch-cs-prefix -mfunction-return=thunk-extern -fno-jump-tables -mharden-sls=all -fpatchable-function-entry=16,16 -fno-delete-null-pointer-checks -O2 -fno-allow-store-data-races -fstack-protector-strong -fno-omit-frame-pointer -fno-optimize-sibling-calls -ftrivial-auto-var-init=zero -fno-stack-clash-protection -fzero-call-used-regs=used-gpr -pg -mrecord-mcount -mfentry -DCC_USING_FENTRY -falign-functions=16 -fstrict-flex-arrays=3 -fno-strict-overflow -fno-stack-check -fconserve-stack -Wall -Wundef -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Werror=strict-prototypes -Wno-format-security -Wno-trigraphs -Wno-frame-address -Wno-address-of-packed-member -Wmissing-declarations -Wmissing-prototypes -Wframe-larger-than=1024 -Wno-main -Wno-dangling-pointer -Wvla -Wno-pointer-sign -Wcast-function-type -Wno-stringop-overflow -Wno-array-bounds -Wno-alloc-size-larger-than -Wimplicit-fallthrough=5 -Werror=date-time -Werror=incompatible-pointer-types -Werror=designated-init -Wenum-conversion -Wextra -Wunused -Wno-unused-but-set-variable -Wno-unused-const-variable -Wno-packed-not-aligned -Wno-format-overflow -Wno-format-truncation -Wno-stringop-truncation -Wno-override-init -Wno-missing-field-initializers -Wno-type-limits -Wno-shift-negative-value -Wno-maybe-uninitialized -Wno-sign-compare -Wno-unused-parameter -g -gdwarf-5 -mno-soft-float -DSUSE_VERSION=0  -fsanitize=bounds-strict -fsanitize=shift -fsanitize=bool -fsanitize=enum  -fsanitize=signed-integer-overflow  -DMODULE  -DKBUILD_BASENAME='"spcm_eeprom_codes"' -DKBUILD_MODNAME='"spcm4"' -D__KBUILD_MODNAME=kmod_spcm4 -c -o /home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o /home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.c   ; ./tools/objtool/objtool --hacks=jump_label --hacks=noinstr --hacks=skylake --retpoline --rethunk --sls --stackval --static-call --uaccess --prefix=16   --module /home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o

source_/home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o := /home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.c

deps_/home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o := \
  include/linux/compiler-version.h \
    $(wildcard include/config/CC_VERSION_TEXT) \
  include/linux/kconfig.h \
    $(wildcard include/config/CPU_BIG_ENDIAN) \
    $(wildcard include/config/BOOGER) \
    $(wildcard include/config/FOO) \
  include/linux/compiler_types.h \
    $(wildcard include/config/DEBUG_INFO_BTF) \
    $(wildcard include/config/PAHOLE_HAS_BTF_TAG) \
    $(wildcard include/config/FUNCTION_ALIGNMENT) \
    $(wildcard include/config/CC_HAS_SANE_FUNCTION_ALIGNMENT) \
    $(wildcard include/config/X86_64) \
    $(wildcard include/config/ARM64) \
    $(wildcard include/config/LD_DEAD_CODE_DATA_ELIMINATION) \
    $(wildcard include/config/LTO_CLANG) \
    $(wildcard include/config/HAVE_ARCH_COMPILER_H) \
    $(wildcard include/config/CC_HAS_COUNTED_BY) \
    $(wildcard include/config/UBSAN_SIGNED_WRAP) \
    $(wildcard include/config/CC_HAS_ASM_INLINE) \
  include/linux/compiler_attributes.h \
  include/linux/compiler-gcc.h \
    $(wildcard include/config/MITIGATION_RETPOLINE) \
    $(wildcard include/config/ARCH_USE_BUILTIN_BSWAP) \
    $(wildcard include/config/SHADOW_CALL_STACK) \
    $(wildcard include/config/KCOV) \
  /home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/../c_header/dlltyp.h \
  /home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.h \

/home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o: $(deps_/home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o)

$(deps_/home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o):

/home/spectroscopist/spcm4/m4i_krnl_linux/../m2i_tools/spcm_eeprom_codes.o: $(wildcard ./tools/objtool/objtool)
