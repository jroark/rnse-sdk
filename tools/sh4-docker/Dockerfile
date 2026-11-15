# An SH-4 GCC for sdk/build.sh where there is none to install (macOS).
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc-sh4-linux-gnu binutils-sh4-linux-gnu && rm -rf /var/lib/apt/lists/*
