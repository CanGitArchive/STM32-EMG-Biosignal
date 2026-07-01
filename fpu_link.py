# fpu_link.py : PlatformIO post-script that adds the hard-float flags to LINKFLAGS (the port and DSP need the FPU).
Import("env")
env.Append(LINKFLAGS=["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"])
