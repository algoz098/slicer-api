#!/bin/bash

# OrcaSlicerCli Build Script
# This script builds the OrcaSlicerCli project and its dependencies

set -e  # Exit on any error

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ORCASLICER_ROOT="$(dirname "$PROJECT_ROOT")/OrcaSlicer"
BUILD_DIR="$PROJECT_ROOT/build"
INSTALL_DIR="$PROJECT_ROOT/install"

# Build configuration
BUILD_TYPE="Release"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
VERBOSE=false
CLEAN=false
DEPS_ONLY=false
CLI_ONLY=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to show usage
show_usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Build OrcaSlicerCli and its dependencies.

OPTIONS:
    -h, --help          Show this help message
    -d, --deps-only     Build dependencies only
    -c, --cli-only      Build CLI only (skip dependencies)
    -j, --jobs N        Number of parallel jobs (default: $JOBS)
    -t, --type TYPE     Build type: Debug, Release, RelWithDebInfo (default: $BUILD_TYPE)
    -v, --verbose       Enable verbose output
    --clean             Clean build directory before building
    --install-dir DIR   Installation directory (default: $INSTALL_DIR)

EXAMPLES:
    $0                  # Build everything
    $0 -d               # Build dependencies only
    $0 -c               # Build CLI only
    $0 -j 8 -t Debug    # Debug build with 8 jobs
    $0 --clean          # Clean build

EOF
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        -d|--deps-only)
            DEPS_ONLY=true
            shift
            ;;
        -c|--cli-only)
            CLI_ONLY=true
            shift
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --install-dir)
            INSTALL_DIR="$2"
            shift 2
            ;;
        *)
            print_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Validate build type
case $BUILD_TYPE in
    Debug|Release|RelWithDebInfo)
        ;;
    *)
        print_error "Invalid build type: $BUILD_TYPE"
        exit 1
        ;;
esac

# Function to check prerequisites
check_prerequisites() {
    print_status "Checking prerequisites..."

    # Check if OrcaSlicer directory exists
    if [[ ! -d "$ORCASLICER_ROOT" ]]; then
        print_error "OrcaSlicer directory not found: $ORCASLICER_ROOT"
        print_error "Please ensure OrcaSlicer is in the parent directory"
        exit 1
    fi

    # Check if OrcaSlicer is built
    if [[ ! -d "$ORCASLICER_ROOT/build" ]] && [[ "$CLI_ONLY" != true ]]; then
        print_warning "OrcaSlicer build directory not found"
        print_warning "OrcaSlicer dependencies may need to be built first"
    fi

    # Check for required tools
    local missing_tools=()
    
    if ! command -v cmake &> /dev/null; then
        missing_tools+=("cmake")
    fi
    
    if ! command -v make &> /dev/null && ! command -v ninja &> /dev/null; then
        missing_tools+=("make or ninja")
    fi

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        print_error "Missing required tools: ${missing_tools[*]}"
        exit 1
    fi

    print_success "Prerequisites check passed"
}

# Function to build OrcaSlicer dependencies
build_orcaslicer_deps() {
    if [[ "$CLI_ONLY" == true ]]; then
        return 0
    fi

    print_status "Building OrcaSlicer dependencies..."

    cd "$ORCASLICER_ROOT"

    # Check if dependencies are already built
    if [[ -d "build/deps" ]]; then
        print_status "OrcaSlicer dependencies already exist, skipping..."
        return 0
    fi

    # Build dependencies
    if [[ -f "build_release_macos.sh" ]] && [[ "$OSTYPE" == "darwin"* ]]; then
        print_status "Building dependencies for macOS..."
        ./build_release_macos.sh -d
    elif [[ -f "build_linux.sh" ]]; then
        print_status "Building dependencies for Linux..."
        ./build_linux.sh -d
    else
        print_error "No suitable build script found for OrcaSlicer dependencies"
        exit 1
    fi

    print_success "OrcaSlicer dependencies built successfully"
}

# Function to build OrcaSlicerCli
build_orcaslicer_cli() {
    if [[ "$DEPS_ONLY" == true ]]; then
        return 0
    fi

    print_status "Building OrcaSlicerCli..."

    cd "$PROJECT_ROOT"

    # Clean build directory if requested
    if [[ "$CLEAN" == true ]] && [[ -d "$BUILD_DIR" ]]; then
        print_status "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
    fi

    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # Configure with CMake
    print_status "Configuring with CMake..."
    
    local cmake_args=(
        "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
        "-DCMAKE_INSTALL_PREFIX=$INSTALL_DIR"
        "-DORCASLICER_ROOT_DIR=$ORCASLICER_ROOT"
    )

    if [[ "$VERBOSE" == true ]]; then
        cmake_args+=("-DCMAKE_VERBOSE_MAKEFILE=ON")
    fi

    cmake "${cmake_args[@]}" ..

    # Build
    print_status "Building OrcaSlicerCli ($BUILD_TYPE with $JOBS jobs)..."
    
    if [[ "$VERBOSE" == true ]]; then
        cmake --build . --config "$BUILD_TYPE" --parallel "$JOBS" --verbose
    else
        cmake --build . --config "$BUILD_TYPE" --parallel "$JOBS"
    fi

    print_success "OrcaSlicerCli built successfully"
}

# Function to install
install_orcaslicer_cli() {
    if [[ "$DEPS_ONLY" == true ]]; then
        return 0
    fi

    print_status "Installing OrcaSlicerCli to $INSTALL_DIR..."
    
    cd "$BUILD_DIR"
    cmake --install . --config "$BUILD_TYPE"
    
    print_success "OrcaSlicerCli installed successfully"
}

# Main execution
main() {
    print_status "Starting OrcaSlicerCli build process..."
    print_status "Build type: $BUILD_TYPE"
    print_status "Jobs: $JOBS"
    print_status "Project root: $PROJECT_ROOT"
    print_status "OrcaSlicer root: $ORCASLICER_ROOT"

    check_prerequisites
    build_orcaslicer_deps
    build_orcaslicer_cli
    install_orcaslicer_cli

    print_success "Build process completed successfully!"
    
    if [[ "$DEPS_ONLY" != true ]]; then
        print_status "Executable location: $INSTALL_DIR/bin/orcaslicer-cli"
        print_status "Run '$INSTALL_DIR/bin/orcaslicer-cli --help' to get started"
    fi
}

# Run main function
main "$@"
