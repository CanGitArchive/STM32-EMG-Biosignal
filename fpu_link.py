# fpu_link.py : PlatformIO post-script. build_flags reach the compiler but NOT the ststm32 link step,
# so add the hard-float flags to LINKFLAGS here. Without them the linker pulls the soft-float crt0/libc
# and the link fails with "uses VFP register arguments, firmware.elf does not". The ARM_CM4F FreeRTOS
# port and our DSP math both need the FPU, so the whole image is hard-float.
Import("env")
env.Append(LINKFLAGS=["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"])
