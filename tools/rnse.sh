# Sourced by the SDK's scripts, with $sdk set to the SDK's directory:
# rnse_bin prints the path of the emulator's command line, `rnse`.
#   1. $RNSE, when set;
#   2. inside the rnse repository (the SDK in its sdk/), the one built
#      from it, built here when needed;
#   3. an `rnse` on the PATH (cargo install --git <the rnse repository>).
rnse_bin() {
    if [ -n "${RNSE:-}" ]; then
        echo "$RNSE"
    elif [ -f "$sdk/../Cargo.toml" ] && grep -q '^name = "rnse"$' "$sdk/../Cargo.toml"; then
        (cd "$sdk/.." && cargo build --release -q --bin rnse) >&2
        echo "$sdk/../target/release/rnse"
    elif command -v rnse >/dev/null 2>&1; then
        command -v rnse
    else
        echo "no rnse: set RNSE to the emulator's binary or put it on the PATH" >&2
        return 1
    fi
}

# An absolute path for a file the caller named, before a script changes
# directory.
abs() {
    case $1 in
        /*) echo "$1" ;;
        *) echo "$PWD/$1" ;;
    esac
}
