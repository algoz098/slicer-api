#!/bin/bash

# OrcaSlicerCli Development Helper Script
# Quick commands for development workflow

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}[DEV]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

show_usage() {
    cat << EOF
OrcaSlicerCli Development Helper

Usage: $0 <command> [options]

Commands:
    build           Quick debug build
    release         Quick release build
    clean           Clean build directory
    test            Run tests (when implemented)
    run             Run the CLI with arguments
    install         Install to local directory
    format          Format code (when implemented)
    lint            Lint code (when implemented)

Examples:
    $0 build                    # Debug build
    $0 release                  # Release build
    $0 run -- --help            # Run CLI with --help
    $0 run -- slice -i test.stl -o test.gcode

EOF
}

cmd_build() {
    print_status "Running debug build..."
    "$SCRIPT_DIR/build.sh" -t Debug -j $(nproc 2>/dev/null || echo 4)
    print_success "Debug build completed"
}

cmd_release() {
    print_status "Running release build..."
    "$SCRIPT_DIR/build.sh" -t Release -j $(nproc 2>/dev/null || echo 4)
    print_success "Release build completed"
}

cmd_clean() {
    print_status "Cleaning build directory..."
    rm -rf "$PROJECT_ROOT/build"
    print_success "Build directory cleaned"
}

cmd_test() {
    print_status "Running tests..."
    # TODO: Implement when tests are available
    echo "Tests not yet implemented"
}

cmd_run() {
    local exe_path="$PROJECT_ROOT/build/bin/orcaslicer-cli"
    
    if [[ ! -f "$exe_path" ]]; then
        print_status "Executable not found, building first..."
        cmd_build
    fi
    
    print_status "Running: $exe_path $*"
    "$exe_path" "$@"
}

cmd_install() {
    print_status "Installing to local directory..."
    "$SCRIPT_DIR/build.sh" --install-dir "$PROJECT_ROOT/install"
    print_success "Installation completed"
}

cmd_format() {
    print_status "Formatting code..."
    # TODO: Implement code formatting
    echo "Code formatting not yet implemented"
}

cmd_lint() {
    print_status "Linting code..."
    # TODO: Implement code linting
    echo "Code linting not yet implemented"
}

# Main command dispatcher
case "${1:-}" in
    build)
        shift
        cmd_build "$@"
        ;;
    release)
        shift
        cmd_release "$@"
        ;;
    clean)
        shift
        cmd_clean "$@"
        ;;
    test)
        shift
        cmd_test "$@"
        ;;
    run)
        shift
        # Handle -- separator
        if [[ "$1" == "--" ]]; then
            shift
        fi
        cmd_run "$@"
        ;;
    install)
        shift
        cmd_install "$@"
        ;;
    format)
        shift
        cmd_format "$@"
        ;;
    lint)
        shift
        cmd_lint "$@"
        ;;
    ""|help|--help|-h)
        show_usage
        ;;
    *)
        echo "Unknown command: $1"
        echo
        show_usage
        exit 1
        ;;
esac
