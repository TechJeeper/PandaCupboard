Import("env")
import os

# Device Guard blocks gcc-ar/gcc-ranlib; GNU ar/ranlib from the same toolchain work.
bindir = os.path.dirname(env.subst("$CC"))
env.Replace(
    AR=os.path.join(bindir, "xtensa-esp32s3-elf-ar.exe"),
    RANLIB=os.path.join(bindir, "xtensa-esp32s3-elf-ranlib.exe"),
)
print(" ** Using GNU ar/ranlib (Device Guard) **")
