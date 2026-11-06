# Android device build environment: what the Makefile needs for the Echo's binaries (NDK r21e, armv7 bionic), the PC
# side (python, libopus for the host build, qemu-arm for tools/qrun.sh), and the firmware tooling (debugfs, cpio).
# Built and run on a remote Docker engine (tools/devenv/devenv.sh): sources travel in as a tar, never as a bind mount.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        make gcc libc6-dev pkg-config libopus-dev zlib1g-dev \
        python3 python3-venv git curl ca-certificates unzip xz-utils file binutils \
        qemu-user e2fsprogs cpio rsync nodejs iproute2 netcat-openbsd bc \
    && rm -rf /var/lib/apt/lists/*
# nodejs: the unit tests of the page's JavaScript (web_crypto, web_matrix, web_tar, band_map, ringbar_run: make unit);
# iproute2: tests/run.py brings up lo in each test's own network namespace (ip link set lo up);
# netcat-openbsd: nc for tests/ota_push_test.sh; bc: a kernel tree's timeconst.h (rook's Wi-Fi driver against Amazon's 3.18)

# NDK r21e: the version the Makefile is written for (NDK ?= toolchain/android-ndk-r21e; GNU as is still in it), held to
# the checksum the release workflow pins (.github/workflows/build.yml NDK_SHA256)
ARG NDK_URL=https://dl.google.com/android/repository/android-ndk-r21e-linux-x86_64.zip
ARG NDK_SHA256=ad7ce5467e18d40050dc51b8e7affc3e635c85bd8c59be62de32352328ed467e
RUN curl -fsSL -o /tmp/ndk.zip "$NDK_URL" \
    && echo "$NDK_SHA256  /tmp/ndk.zip" | sha256sum -c - \
    && unzip -q /tmp/ndk.zip -d /opt && rm /tmp/ndk.zip
ENV NDK=/opt/android-ndk-r21e

# uv for the repo's .venv (pyproject.toml + uv.lock): one named release's installer, not whatever is newest that day
ARG UV_VERSION=0.13.7
RUN curl -fsSL "https://astral.sh/uv/$UV_VERSION/install.sh" | env UV_INSTALL_DIR=/usr/local/bin sh

WORKDIR /src
