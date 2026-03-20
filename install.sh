#!/bin/sh
# ptyshot install script — https://github.com/sa3lej/ptyshot
# Usage: curl -fsSL https://raw.githubusercontent.com/sa3lej/ptyshot/main/install.sh | sh
set -e

REPO="sa3lej/ptyshot"
BINARY="ptyshot"

# Pick an install directory that's already in PATH
detect_install_dir() {
  for dir in /usr/local/bin "$HOME/.local/bin" "$HOME/bin"; do
    case ":$PATH:" in
      *":${dir}:"*) [ -d "$dir" ] && [ -w "$dir" ] && echo "$dir" && return ;;
    esac
  done
  # fallback
  echo "$HOME/.local/bin"
}

# --- helpers ---

say() { printf '  %s\n' "$*"; }
err() { printf '  ERROR: %s\n' "$*" >&2; exit 1; }

need() {
  command -v "$1" >/dev/null 2>&1 || err "$1 is required but not found"
}

# --- detect platform ---

detect_os() {
  case "$(uname -s)" in
    Linux*)  echo "linux" ;;
    Darwin*) echo "darwin" ;;
    *)       err "Unsupported OS: $(uname -s)" ;;
  esac
}

detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64)    echo "x86_64" ;;
    aarch64|arm64)   echo "arm64" ;;
    *)               err "Unsupported architecture: $(uname -m)" ;;
  esac
}

# --- resolve latest version ---

latest_version() {
  need curl
  curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
    | grep '"tag_name"' \
    | head -1 \
    | sed 's/.*"tag_name": *"//;s/".*//'
}

# --- build from source fallback ---

build_from_source() {
  say "Falling back to building from source..."
  for cmd in git make cc; do
    command -v "$cmd" >/dev/null 2>&1 || err "$cmd is required to build from source"
  done

  TMPDIR="$(mktemp -d)"
  trap 'rm -rf "$TMPDIR"' EXIT

  say "Cloning repository..."
  git clone --depth 1 "https://github.com/${REPO}.git" "${TMPDIR}/ptyshot" 2>/dev/null

  say "Building..."
  make -C "${TMPDIR}/ptyshot" >/dev/null

  install_binary "${TMPDIR}/ptyshot/ptyshot"
}

# --- install binary to detected dir ---

install_binary() {
  BINARY_PATH="$1"
  chmod +x "$BINARY_PATH"
  mkdir -p "$INSTALL_DIR"

  say "Installing to ${INSTALL_DIR}/${BINARY}..."
  if [ -w "$INSTALL_DIR" ]; then
    mv -f "$BINARY_PATH" "${INSTALL_DIR}/${BINARY}"
  else
    sudo mv -f "$BINARY_PATH" "${INSTALL_DIR}/${BINARY}"
  fi

  say "Done! $(${INSTALL_DIR}/${BINARY} --version)"
  case ":$PATH:" in
    *":${INSTALL_DIR}:"*) ;;
    *) say "Note: Add ${INSTALL_DIR} to your PATH if '${BINARY}' is not found." ;;
  esac
  printf '\n'
}

# --- main ---

main() {
  INSTALL_DIR="$(detect_install_dir)"

  printf '\n  ptyshot installer\n\n'

  OS="$(detect_os)"
  ARCH="$(detect_arch)"
  say "OS:   $OS"
  say "Arch: $ARCH"

  # Try pre-built binary first
  if command -v curl >/dev/null 2>&1 && command -v unzip >/dev/null 2>&1; then
    say "Fetching latest version..."
    VERSION="$(latest_version)" || VERSION=""

    if [ -n "$VERSION" ]; then
      say "Version: $VERSION"

      ASSET="ptyshot-${VERSION}-${OS}-${ARCH}.zip"
      URL="https://github.com/${REPO}/releases/download/${VERSION}/${ASSET}"

      TMPDIR="$(mktemp -d)"
      trap 'rm -rf "$TMPDIR"' EXIT

      say "Downloading ${ASSET}..."
      if curl -fsSL -o "${TMPDIR}/${ASSET}" "$URL" 2>/dev/null; then
        say "Extracting..."
        unzip -qo "${TMPDIR}/${ASSET}" -d "${TMPDIR}"

        EXTRACTED=$(find "${TMPDIR}" -name "$BINARY" -type f ! -name '*.zip' | head -1)
        if [ -n "$EXTRACTED" ]; then
          install_binary "$EXTRACTED"
          return
        fi
      fi

      say "Pre-built binary not available for ${OS}-${ARCH}"
    fi
  fi

  # Fallback: build from source
  build_from_source
}

main
