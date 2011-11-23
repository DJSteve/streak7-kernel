cmd_arch/arm/mach-tegra/cortex-a9.o := arm-eabi-gcc -Wp,-MD,arch/arm/mach-tegra/.cortex-a9.o.d  -nostdinc -isystem /prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/../lib/gcc/arm-eabi/4.4.0/include -I/adam/streak7/streak7-kernel/arch/arm/include -Iinclude  -include include/generated/autoconf.h -D__KERNEL__ -mlittle-endian -Iarch/arm/mach-tegra/include -D__ASSEMBLY__ -mabi=aapcs-linux -mno-thumb-interwork -funwind-tables  -D__LINUX_ARM_ARCH__=7 -march=armv7-a  -include asm/unified.h -mfloat-abi=softfp -mfpu=vfpv3-d16 -gdwarf-2        -c -o arch/arm/mach-tegra/cortex-a9.o arch/arm/mach-tegra/cortex-a9.S

deps_arch/arm/mach-tegra/cortex-a9.o := \
  arch/arm/mach-tegra/cortex-a9.S \
    $(wildcard include/config/cache/l2x0.h) \
    $(wildcard include/config/vfpv3.h) \
    $(wildcard include/config/trusted/foundations.h) \
    $(wildcard include/config/smp.h) \
    $(wildcard include/config/alignment/trap.h) \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/unified.h \
    $(wildcard include/config/arm/asm/unified.h) \
    $(wildcard include/config/thumb2/kernel.h) \
  include/linux/linkage.h \
  include/linux/compiler.h \
    $(wildcard include/config/trace/branch/profiling.h) \
    $(wildcard include/config/profile/all/branches.h) \
    $(wildcard include/config/enable/must/check.h) \
    $(wildcard include/config/enable/warn/deprecated.h) \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/linkage.h \
  include/linux/init.h \
    $(wildcard include/config/modules.h) \
    $(wildcard include/config/hotplug.h) \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/assembler.h \
    $(wildcard include/config/cpu/feroceon.h) \
    $(wildcard include/config/trace/irqflags.h) \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/ptrace.h \
    $(wildcard include/config/cpu/endian/be8.h) \
    $(wildcard include/config/arm/thumb.h) \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/hwcap.h \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/domain.h \
    $(wildcard include/config/io/36.h) \
    $(wildcard include/config/mmu.h) \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/cache.h \
    $(wildcard include/config/arm/l1/cache/shift.h) \
    $(wildcard include/config/aeabi.h) \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/vfpmacros.h \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/vfp.h \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/hardware/cache-l2x0.h \
  arch/arm/mach-tegra/include/mach/iomap.h \
    $(wildcard include/config/tegra/debug/uart/none.h) \
    $(wildcard include/config/tegra/debug/uarta.h) \
    $(wildcard include/config/tegra/debug/uartb.h) \
    $(wildcard include/config/tegra/debug/uartc.h) \
    $(wildcard include/config/tegra/debug/uartd.h) \
    $(wildcard include/config/tegra/debug/uarte.h) \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/sizes.h \
  arch/arm/mach-tegra/include/mach/io.h \
  arch/arm/mach-tegra/power.h \
  /adam/streak7/streak7-kernel/arch/arm/include/asm/page.h \
    $(wildcard include/config/cpu/copy/v3.h) \
    $(wildcard include/config/cpu/copy/v4wt.h) \
    $(wildcard include/config/cpu/copy/v4wb.h) \
    $(wildcard include/config/cpu/copy/feroceon.h) \
    $(wildcard include/config/cpu/copy/fa.h) \
    $(wildcard include/config/cpu/sa1100.h) \
    $(wildcard include/config/cpu/xscale.h) \
    $(wildcard include/config/cpu/xsc3.h) \
    $(wildcard include/config/cpu/copy/v6.h) \
    $(wildcard include/config/sparsemem.h) \
  include/asm-generic/getorder.h \
  arch/arm/mach-tegra/power-macros.S \

arch/arm/mach-tegra/cortex-a9.o: $(deps_arch/arm/mach-tegra/cortex-a9.o)

$(deps_arch/arm/mach-tegra/cortex-a9.o):
