#!/usr/bin/env bash
set -euo pipefail

# Install the host packages and pinned ESP-IDF toolchain needed by firmware/.
# Run from any directory: ./scripts/install-dependencies.sh

IDF_VERSION="${IDF_VERSION:-v5.3.1}"
BLUEPAD32_VERSION="${BLUEPAD32_VERSION:-e9b755faabc240585da42e6d26164bb2cdd064d3}"
USER_HOME="$(getent passwd "$(id -u)" 2>/dev/null | cut -d: -f6 || true)"
USER_HOME="${USER_HOME:-${HOME:-}}"
IDF_PATH="${IDF_PATH:-${USER_HOME}/esp/esp-idf}"
BLUEPAD32_PATH="${BLUEPAD32_PATH:-${USER_HOME}/esp/bluepad32}"

if [[ -z "${USER_HOME}" ]]; then
  echo "Unable to determine the current user's home directory." >&2
  exit 1
fi

install_linux_packages() {
  local packages=(git wget flex bison gperf python3 python3-pip python3-venv
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0)

  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y "${packages[@]}"
  elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y git wget flex bison gperf python3 python3-pip \
      python3-virtualenv cmake ninja-build ccache dfu-util libusbx
  else
    echo "Unsupported Linux package manager; install ESP-IDF prerequisites manually." >&2
    return 1
  fi
}

case "$(uname -s)" in
  Linux)
    install_linux_packages
    ;;
  Darwin)
    if ! command -v brew >/dev/null 2>&1; then
      echo "Homebrew is required on macOS: https://brew.sh/" >&2
      exit 1
    fi
    brew install cmake ninja dfu-util python3
    ;;
  *)
    echo "This script supports Linux and macOS. See firmware/README.md for Windows." >&2
    exit 1
    ;;
esac

mkdir -p "$(dirname "${IDF_PATH}")"
if [[ -e "${IDF_PATH}/.git" ]]; then
  git -C "${IDF_PATH}" fetch --tags --quiet
else
  git clone --branch "${IDF_VERSION}" --recursive \
    https://github.com/espressif/esp-idf.git "${IDF_PATH}"
fi

if [[ "$(git -C "${IDF_PATH}" rev-parse --abbrev-ref HEAD)" != "${IDF_VERSION}" ]]; then
  git -C "${IDF_PATH}" checkout --quiet "${IDF_VERSION}"
fi
git -C "${IDF_PATH}" submodule update --init --recursive

"${IDF_PATH}/install.sh" esp32

if [[ -e "${BLUEPAD32_PATH}/.git" ]]; then
  git -C "${BLUEPAD32_PATH}" fetch --quiet origin
else
  git clone --quiet --recursive https://github.com/ricardoquesada/bluepad32.git \
    "${BLUEPAD32_PATH}"
fi
git -C "${BLUEPAD32_PATH}" checkout --quiet "${BLUEPAD32_VERSION}"
git -C "${BLUEPAD32_PATH}" submodule update --init --recursive --quiet

# Bluepad32's official integration script installs its BTstack ESP-IDF
# component under IDF_PATH/components/btstack.
. "${IDF_PATH}/export.sh"
cd "${BLUEPAD32_PATH}/external/btstack/port/esp32"
IDF_PATH="${IDF_PATH}" ./integrate_btstack.py

cat <<EOF

Dependencies installed. In each new shell, activate ESP-IDF with:
  . "${IDF_PATH}/export.sh"

Then build and flash:
  cd firmware
  idf.py set-target esp32
  idf.py -p /dev/ttyACM0 flash monitor
EOF
