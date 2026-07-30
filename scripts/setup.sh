#!/usr/bin/env bash
# OpenGPU dependency installation and environment setup.
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)
MILL_VERSION=1.1.2
readonly VERILATOR_VERSION="v4.216"

usage() {
  cat <<'EOF'
Usage: scripts/setup.sh <check|deps|java|mill|sbt|verilator|gtkwave|toolchain|all>
  check     report required tools and versions
  deps      install native build prerequisites (Debian/Ubuntu)
  java      install the default OpenJDK development kit
  mill      install Mill 1.1.2 in /usr/local/bin
  sbt       install the SBT launcher in /usr/local/bin
  verilator install Verilator 4.216 from the official source release
  gtkwave   install GTKWave for viewing generated VCD files
  toolchain install the Debian riscv64-linux-gnu toolchain
  all       install dependencies, then configure the environment
EOF
}

need_apt() { command -v apt-get >/dev/null || { echo 'setup: apt-get is required' >&2; return 1; }; }
as_root() {
  if [[ ${EUID} -eq 0 ]]; then "$@"; else command sudo "$@"; fi
}
make_temp_dir() {
  local __result_var=$1
  local temp_dir
  temp_dir=$(mktemp -d)
  printf -v "${__result_var}" '%s' "${temp_dir}"
}
install_deps() { need_apt; as_root apt-get update; as_root apt-get install -y build-essential autoconf flex bison libfl-dev libtool curl git python3 libncurses-dev; }
install_java() { need_apt; as_root apt-get install -y default-jdk; }
install_verilator() {
  if command -v verilator >/dev/null 2>&1 &&
     verilator --version | grep -Fq "Verilator 4.216"; then
    echo "Verilator 4.216 is already installed."
    return
  fi

  local work
  make_temp_dir work
  git clone --depth 1 --branch "$VERILATOR_VERSION" \
    https://github.com/verilator/verilator.git "$work/verilator"
  (
    cd "$work/verilator"
    autoconf
    ./configure
    make -j"$(nproc)"
    as_root make install
  )
  rm -rf "$work"
}
install_gtkwave() { need_apt; as_root apt-get install -y gtkwave; }
install_riscv() { need_apt; as_root apt-get install -y gcc-riscv64-linux-gnu g++-riscv64-linux-gnu binutils-riscv64-linux-gnu; }
install_mill() {
  local work
  make_temp_dir work

  # The launcher reads //| mill-version from each build.mill.
  curl -fsSL "https://raw.githubusercontent.com/com-lihaoyi/mill/${MILL_VERSION}/mill" \
    -o "${work}/mill"
  chmod +x "${work}/mill"
  as_root install -m 0755 "${work}/mill" /usr/local/bin/mill

  grep -Fqx "//| mill-version: ${MILL_VERSION}" "${REPO_ROOT}/hw/chisel/build.mill"
  grep -Fqx "//| mill-version: ${MILL_VERSION}" "${REPO_ROOT}/hw/spinal/build.mill"
  rm -rf "${work}"
  echo "Installed the Mill launcher; both backends select Mill ${MILL_VERSION}."
}
install_sbt() {
  local version=1.10.7 archive=/tmp/opengpu-sbt.tgz
  curl --fail --location --output "${archive}" "https://github.com/sbt/sbt/releases/download/v${version}/sbt-${version}.tgz"
  tar -xzf "${archive}" -C /tmp
  as_root install -d /opt/sbt
  as_root cp -a /tmp/sbt/. /opt/sbt/
  as_root ln -sfn /opt/sbt/bin/sbt /usr/local/bin/sbt
  rm -rf /tmp/sbt "${archive}"
}
check() {
  local missing=0 tool
  for tool in make gcc g++ java mill sbt gtkwave bison flex riscv64-linux-gnu-gcc riscv64-linux-gnu-g++ riscv64-linux-gnu-objcopy riscv64-linux-gnu-objdump; do
    if command -v "${tool}" >/dev/null; then
      printf '%-26s %s\n' "${tool}" "$(command -v "${tool}")"
    else
      printf '%-26s MISSING\n' "${tool}"
      missing=1
    fi
  done
  if command -v verilator >/dev/null 2>&1 &&
     verilator --version | grep -Fq 'Verilator 4.216'; then
    printf '%-26s %s\n' verilator "$(verilator --version)"
  else
    printf '%-26s MISSING or wrong version (need Verilator 4.216)\n' verilator
    missing=1
  fi
  return "${missing}"
}

main() {
  case "${1:-}" in
    check) check ;;
    deps) install_deps ;;
    java) install_java ;;
    mill) install_mill ;;
    sbt) install_sbt ;;
    verilator) install_verilator ;;
    gtkwave) install_gtkwave ;;
    toolchain) install_riscv ;;
    all) install_deps; install_java; install_mill; install_sbt; install_verilator; install_gtkwave; install_riscv; check ;;
    *) usage; return 2 ;;
  esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
