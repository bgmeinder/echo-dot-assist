#!/usr/bin/env bash
# Android build environment on a Docker engine, local or remote (DEVENV_CONTEXT): a remote one cannot see this PC's files,
# so the checkout goes into a named volume as a tar, the build runs there, and build/ comes back the same way.
#
#   tools/devenv/devenv.sh image                 build the image (tools/devenv/Dockerfile)
#   tools/devenv/devenv.sh sync                  the checkout into the volume (no .git, build/, firmware/, toolchain/)
#   tools/devenv/devenv.sh firmware <codename>   firmware/<codename>/rootfs/system/lib (what the build links against)
#   tools/devenv/devenv.sh run <command...>      a command in /src, e.g. run make DEVICE=rook
#   tools/devenv/devenv.sh fetch [<codename>]    build/ (or build/<codename>/) back into this checkout
#   tools/devenv/devenv.sh shell                 an interactive shell in /src
# The unit tests that use Python (make unit: ws, noise, wakealign...) want the repo's .venv in the volume, kept by sync:
#   tools/devenv/devenv.sh run uv sync --python 3.13   (uv's newest CPython, 3.15, has no wheels for the locked litert;
#   its Python goes to toolchain/uv-python in the volume, so .venv lasts from run to run)
#
# DEVENV_CONTEXT (the current Docker context), DEVENV_IMAGE (android-build), DEVENV_VOLUME (echo-dot-assist-src) change
# the defaults.
set -euo pipefail
export MSYS_NO_PATHCONV=1   # Git Bash on Windows would turn /src into C:/Program Files/Git/src
CTX=${DEVENV_CONTEXT:-$(docker context show 2>/dev/null || echo default)}
IMG=${DEVENV_IMAGE:-android-build}
VOL=${DEVENV_VOLUME:-echo-dot-assist-src}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
dk() { docker --context "$CTX" "$@"; }
# seccomp off: qemu-arm running bionic binaries (tools/qrun.sh, magiskpolicy) calls personality(), which the default
# profile refuses ("error getting old personality value")
# UV_PYTHON_INSTALL_DIR: the Python uv installs for .venv lives in the volume too (toolchain/, kept by sync); in the
# container's own filesystem it went with the container, and .venv/bin/python pointed at nothing on the next run
inside() { dk run --rm -i --security-opt seccomp=unconfined -e UV_PYTHON_INSTALL_DIR=/src/toolchain/uv-python -v "$VOL:/src" -w /src "$IMG" "$@"; }

case "${1:-}" in
image)
    dk build -t "$IMG" "$ROOT/tools/devenv" ;;
sync)
    # tracked and untracked files alike, minus what is big or machine-local
    tar -C "$ROOT" -c --exclude=./.git --exclude=./build --exclude=./firmware --exclude=./toolchain \
        --exclude=./.venv --exclude=./testdata/corpus . |
        inside sh -c 'find /src -mindepth 1 -maxdepth 1 ! -name firmware ! -name build ! -name toolchain ! -name .venv -exec rm -rf {} +; tar -x -C /src' ;;
firmware)
    dev=${2:?codename}
    tar -C "$ROOT" -c "firmware/$dev/rootfs/system/lib" | inside sh -c "rm -rf firmware/$dev && tar -x -C /src" ;;
run)
    shift; inside "$@" ;;
fetch)
    sub=build${2:+/$2}
    # not the kernel trees prepared for modules (ktree: a symlink to the sources, which Windows cannot make) or the
    # module sources copied to build them: what comes back is what was built
    inside tar -c --exclude='*/ktree' --exclude='*/wifidrv' --exclude='*/kmod' "$sub" | tar -C "$ROOT" -x ;;
shell)
    dk run --rm -it -v "$VOL:/src" -w /src "$IMG" bash ;;
*)
    sed -n '2,13p' "$0"; exit 2 ;;
esac
