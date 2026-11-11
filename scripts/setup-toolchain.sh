#!/usr/bin/env bash
# Reproducible headless setup of the Vita porting toolchain on Linux.
# Installs VitaSDK + all vdpm packages, clones the debug/agent tooling,
# builds the on-console plugins, and ports the 3.65 DevKit kd modules.
#
# Usage:  scripts/setup-toolchain.sh            (idempotent; safe to re-run)
# Env:    VITASDK (default /usr/local/vitasdk), WORK (default $HOME/vita)
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
export VDPM_NONINTERACTIVE=1
WORK="${WORK:-$HOME/vita}"
mkdir -p "$WORK"; cd "$WORK"

clone() { # clone URL DIR [extra git args...]
  local url=$1 dir=$2; shift 2
  [ -d "$dir/.git" ] || git clone --depth 1 "$@" "$url" "$dir"
}

# 1. VitaSDK toolchain + every vdpm package ---------------------------------
clone https://github.com/vitasdk/vdpm vdpm
if [ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]; then
  (cd vdpm && ./bootstrap-vitasdk.sh)
fi
# vdpm's install-all.sh calls the repo's legacy wrapper, which can't find the
# new pacman location; drive the SDK's own vdpm binary with the same list.
PKGS=$(sed -n '/PACKAGES=(/,/^)/p' vdpm/include/install-packages.sh | grep -vE 'PACKAGES|^\)' | tr -d '\t ')
for p in $PKGS; do
  # openssl (1.0.2) conflicts with openssl-1.1.1 that curl pulls in: skip it.
  [ "$p" = openssl ] && continue
  vdpm list "$p" >/dev/null 2>&1 || vdpm install "$p" || echo "WARN: $p failed"
done

# 2. Debug / agent tooling ---------------------------------------------------
clone https://github.com/Grarak/psp2-tooling psp2-tooling
clone https://github.com/shoui520/vita-agent-use vita-agent-use
clone https://github.com/Mbed-TLS/mbedtls mbedtls-3.6.7 -b v3.6.7 --recurse-submodules --shallow-submodules
if [ ! -d psvita-elfs/.git ]; then
  git clone --depth 1 --filter=blob:none --sparse -b 365-TOOL https://github.com/LiEnby/psvita-elfs
  (cd psvita-elfs && git sparse-checkout set os0/kd)
fi
python3 -m pip install -q cryptography

# 3. vita-agent-use plugins (MinSizeRel: Release -O3 trips vita-elf-create's
#    "segment 1 overlaps" on vita_agent_shell with GCC 15) -------------------
cmake -S vita-agent-use -B vita-agent-use/build -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DVAU_MBEDTLS_SOURCE="$WORK/mbedtls-3.6.7"
cmake --build vita-agent-use/build -j"$(nproc)"

# 4. psp2-tooling retail-cp + ported DevKit modules --------------------------
cmake -S psp2-tooling/retail-cp -B psp2-tooling/build/retail-cp
cmake --build psp2-tooling/build/retail-cp -j"$(nproc)"
mkdir -p devkit-kd
for f in psvita-elfs/os0/kd/*.skprx.elf; do   # porter wants <name>.elf
  ln -sf "$(realpath "$f")" "devkit-kd/$(basename "$f" .skprx.elf).elf"
done
rm -rf psp2-tooling/build/retail-modules
python3 psp2-tooling/retail-cp/port_modules.py --modules "$WORK/devkit-kd" \
  --out "$WORK/psp2-tooling/build/retail-modules"

echo "Toolchain ready. export VITASDK=$VITASDK PATH=\$VITASDK/bin:\$PATH"
