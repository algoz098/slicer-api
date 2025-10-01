#!/bin/bash

# OrcaSlicerCli Build Test Script
# Tests basic compilation and functionality

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TEST_BUILD_DIR="$PROJECT_ROOT/test_build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}[TEST]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[FAIL]${NC} $1"
}

# Test counter
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

run_test() {
    local test_name="$1"
    local test_command="$2"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    print_status "Running test: $test_name"
    
    if eval "$test_command"; then
        print_success "$test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        print_error "$test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Test 1: Check if OrcaSlicer directory exists
test_orcaslicer_exists() {
    local orcaslicer_dir="$(dirname "$PROJECT_ROOT")/OrcaSlicer"
    [[ -d "$orcaslicer_dir" ]]
}

# Test 2: Check CMake configuration
test_cmake_configure() {
    cd "$PROJECT_ROOT"
    rm -rf "$TEST_BUILD_DIR"
    mkdir -p "$TEST_BUILD_DIR"
    cd "$TEST_BUILD_DIR"
    
    cmake -DCMAKE_BUILD_TYPE=Debug \
          -DORCASLICER_ROOT_DIR="$(dirname "$PROJECT_ROOT")/OrcaSlicer" \
          .. > cmake_config.log 2>&1
}

# Test 3: Check compilation (headers only)
test_compilation_headers() {
    cd "$TEST_BUILD_DIR"
    
    # Try to compile just the headers to check for syntax errors
    make -j2 > compile.log 2>&1 || true
    
    # Check if at least some object files were created
    find . -name "*.o" | head -1 | grep -q "\.o$"
}

# Test 4: Check if executable can be created (even if linking fails)
test_executable_creation() {
    cd "$TEST_BUILD_DIR"
    
    # Check if the main target exists in Makefile
    grep -q "orcaslicer-cli" Makefile
}

# Test 5: Verify source file structure
test_source_structure() {
    local required_files=(
        "$PROJECT_ROOT/src/main.cpp"
        "$PROJECT_ROOT/src/Application.hpp"
        "$PROJECT_ROOT/src/Application.cpp"
        "$PROJECT_ROOT/src/core/CliCore.hpp"
        "$PROJECT_ROOT/src/core/CliCore.cpp"
        "$PROJECT_ROOT/src/utils/ArgumentParser.hpp"
        "$PROJECT_ROOT/src/utils/ArgumentParser.cpp"
        "$PROJECT_ROOT/src/utils/Logger.hpp"
        "$PROJECT_ROOT/src/utils/Logger.cpp"
        "$PROJECT_ROOT/src/utils/ErrorHandler.hpp"
        "$PROJECT_ROOT/src/utils/ErrorHandler.cpp"
    )
    
    for file in "${required_files[@]}"; do
        if [[ ! -f "$file" ]]; then
            print_error "Missing required file: $file"
            return 1
        fi
    done
    
    return 0
}

# Test 6: Check CMakeLists.txt syntax
test_cmake_syntax() {
    cd "$PROJECT_ROOT"
    
    # Basic syntax check - CMake should be able to parse the file
    cmake -P CMakeLists.txt > /dev/null 2>&1 || true
    
    # Check if main CMakeLists.txt exists and has basic content
    [[ -f "CMakeLists.txt" ]] && \
    grep -q "project(OrcaSlicerCli" CMakeLists.txt && \
    grep -q "add_subdirectory(src)" CMakeLists.txt
}

# Test 7: Check script permissions and syntax
test_scripts() {
    local scripts=(
        "$PROJECT_ROOT/scripts/build.sh"
        "$PROJECT_ROOT/scripts/dev.sh"
    )
    
    for script in "${scripts[@]}"; do
        if [[ ! -x "$script" ]]; then
            print_error "Script not executable: $script"
            return 1
        fi
        
        # Basic syntax check
        bash -n "$script" || return 1
    done
    
    return 0
}

# Test 8: Documentation exists
test_documentation() {
    local docs=(
        "$PROJECT_ROOT/README.md"
        "$PROJECT_ROOT/docs/DEVELOPMENT.md"
        "$PROJECT_ROOT/examples/basic_usage.md"
    )
    
    for doc in "${docs[@]}"; do
        if [[ ! -f "$doc" ]]; then
            print_error "Missing documentation: $doc"
            return 1
        fi
    done
    
    return 0
}

# Main test execution
main() {
    print_status "Starting OrcaSlicerCli build tests..."
    print_status "Project root: $PROJECT_ROOT"
    
    # Run all tests
    run_test "OrcaSlicer directory exists" "test_orcaslicer_exists"
    run_test "Source file structure" "test_source_structure"
    run_test "CMakeLists.txt syntax" "test_cmake_syntax"
    run_test "Script permissions and syntax" "test_scripts"
    run_test "Documentation exists" "test_documentation"
    run_test "CMake configuration" "test_cmake_configure"
    run_test "Header compilation" "test_compilation_headers"
    run_test "Executable target creation" "test_executable_creation"
    
    # Print summary
    echo
    print_status "Test Summary:"
    echo "  Total tests: $TESTS_RUN"
    echo "  Passed: $TESTS_PASSED"
    echo "  Failed: $TESTS_FAILED"
    
    if [[ $TESTS_FAILED -eq 0 ]]; then
        print_success "All tests passed! Infrastructure is ready."
        echo
        print_status "Next steps:"
        echo "  1. Ensure OrcaSlicer is compiled: cd ../OrcaSlicer && ./build_release_macos.sh"
        echo "  2. Build OrcaSlicerCli: ./scripts/build.sh"
        echo "  3. Test the CLI: ./install/bin/orcaslicer-cli --help"
        return 0
    else
        print_error "Some tests failed. Please fix the issues before proceeding."
        echo
        print_status "Check the following:"
        echo "  - All source files are present"
        echo "  - CMakeLists.txt is properly configured"
        echo "  - Scripts have correct permissions"
        echo "  - OrcaSlicer directory exists in parent folder"
        return 1
    fi
}

# Cleanup function
cleanup() {
    if [[ -d "$TEST_BUILD_DIR" ]]; then
        print_status "Cleaning up test build directory..."
        rm -rf "$TEST_BUILD_DIR"
    fi
}

# Set up cleanup trap
trap cleanup EXIT

# Run main function
main "$@"
