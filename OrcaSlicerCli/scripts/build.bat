@echo off
REM OrcaSlicerCli Build Script for Windows
REM This script builds the OrcaSlicerCli project and its dependencies

setlocal enabledelayedexpansion

REM Script configuration
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "ORCASLICER_ROOT=%PROJECT_ROOT%\..\OrcaSlicer"
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "INSTALL_DIR=%PROJECT_ROOT%\install"

REM Build configuration
set "BUILD_TYPE=Release"
set "JOBS=%NUMBER_OF_PROCESSORS%"
set "VERBOSE=false"
set "CLEAN=false"
set "DEPS_ONLY=false"
set "CLI_ONLY=false"
set "GENERATOR=Visual Studio 17 2022"

REM Function to print colored output
:print_status
echo [INFO] %~1
goto :eof

:print_success
echo [SUCCESS] %~1
goto :eof

:print_warning
echo [WARNING] %~1
goto :eof

:print_error
echo [ERROR] %~1
goto :eof

REM Function to show usage
:show_usage
echo Usage: %~nx0 [OPTIONS]
echo.
echo Build OrcaSlicerCli and its dependencies.
echo.
echo OPTIONS:
echo     /h, /help           Show this help message
echo     /d, /deps-only      Build dependencies only
echo     /c, /cli-only       Build CLI only (skip dependencies)
echo     /j:N, /jobs:N       Number of parallel jobs (default: %JOBS%)
echo     /t:TYPE, /type:TYPE Build type: Debug, Release, RelWithDebInfo (default: %BUILD_TYPE%)
echo     /v, /verbose        Enable verbose output
echo     /clean              Clean build directory before building
echo     /install-dir:DIR    Installation directory (default: %INSTALL_DIR%)
echo     /generator:GEN      CMake generator (default: %GENERATOR%)
echo.
echo EXAMPLES:
echo     %~nx0                    # Build everything
echo     %~nx0 /d                 # Build dependencies only
echo     %~nx0 /c                 # Build CLI only
echo     %~nx0 /j:8 /t:Debug      # Debug build with 8 jobs
echo     %~nx0 /clean             # Clean build
echo.
goto :eof

REM Parse command line arguments
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="/h" goto :show_help
if /i "%~1"=="/help" goto :show_help
if /i "%~1"=="/d" set "DEPS_ONLY=true" & shift & goto :parse_args
if /i "%~1"=="/deps-only" set "DEPS_ONLY=true" & shift & goto :parse_args
if /i "%~1"=="/c" set "CLI_ONLY=true" & shift & goto :parse_args
if /i "%~1"=="/cli-only" set "CLI_ONLY=true" & shift & goto :parse_args
if /i "%~1"=="/v" set "VERBOSE=true" & shift & goto :parse_args
if /i "%~1"=="/verbose" set "VERBOSE=true" & shift & goto :parse_args
if /i "%~1"=="/clean" set "CLEAN=true" & shift & goto :parse_args

REM Handle options with values
set "arg=%~1"
if /i "!arg:~0,3!"=="/j:" set "JOBS=!arg:~3!" & shift & goto :parse_args
if /i "!arg:~0,6!"=="/jobs:" set "JOBS=!arg:~6!" & shift & goto :parse_args
if /i "!arg:~0,3!"=="/t:" set "BUILD_TYPE=!arg:~3!" & shift & goto :parse_args
if /i "!arg:~0,6!"=="/type:" set "BUILD_TYPE=!arg:~6!" & shift & goto :parse_args
if /i "!arg:~0,13!"=="/install-dir:" set "INSTALL_DIR=!arg:~13!" & shift & goto :parse_args
if /i "!arg:~0,11!"=="/generator:" set "GENERATOR=!arg:~11!" & shift & goto :parse_args

call :print_error "Unknown option: %~1"
goto :show_help

:show_help
call :show_usage
exit /b 1

:args_done

REM Validate build type
if /i "%BUILD_TYPE%"=="Debug" goto :build_type_ok
if /i "%BUILD_TYPE%"=="Release" goto :build_type_ok
if /i "%BUILD_TYPE%"=="RelWithDebInfo" goto :build_type_ok
call :print_error "Invalid build type: %BUILD_TYPE%"
exit /b 1
:build_type_ok

REM Function to check prerequisites
:check_prerequisites
call :print_status "Checking prerequisites..."

REM Check if OrcaSlicer directory exists
if not exist "%ORCASLICER_ROOT%" (
    call :print_error "OrcaSlicer directory not found: %ORCASLICER_ROOT%"
    call :print_error "Please ensure OrcaSlicer is in the parent directory"
    exit /b 1
)

REM Check if OrcaSlicer is built
if not exist "%ORCASLICER_ROOT%\build" (
    if "%CLI_ONLY%"=="false" (
        call :print_warning "OrcaSlicer build directory not found"
        call :print_warning "OrcaSlicer dependencies may need to be built first"
    )
)

REM Check for required tools
where cmake >nul 2>&1
if errorlevel 1 (
    call :print_error "CMake not found in PATH"
    exit /b 1
)

call :print_success "Prerequisites check passed"
goto :eof

REM Function to build OrcaSlicer dependencies
:build_orcaslicer_deps
if "%CLI_ONLY%"=="true" goto :eof

call :print_status "Building OrcaSlicer dependencies..."

pushd "%ORCASLICER_ROOT%"

REM Check if dependencies are already built
if exist "build\deps" (
    call :print_status "OrcaSlicer dependencies already exist, skipping..."
    popd
    goto :eof
)

REM Build dependencies
if exist "build_release_vs2022.bat" (
    call :print_status "Building dependencies for Windows..."
    call build_release_vs2022.bat deps
) else (
    call :print_error "No suitable build script found for OrcaSlicer dependencies"
    popd
    exit /b 1
)

popd
call :print_success "OrcaSlicer dependencies built successfully"
goto :eof

REM Function to build OrcaSlicerCli
:build_orcaslicer_cli
if "%DEPS_ONLY%"=="true" goto :eof

call :print_status "Building OrcaSlicerCli..."

pushd "%PROJECT_ROOT%"

REM Clean build directory if requested
if "%CLEAN%"=="true" (
    if exist "%BUILD_DIR%" (
        call :print_status "Cleaning build directory..."
        rmdir /s /q "%BUILD_DIR%"
    )
)

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"

REM Configure with CMake
call :print_status "Configuring with CMake..."

set "CMAKE_ARGS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% -DORCASLICER_ROOT_DIR=%ORCASLICER_ROOT%"

if "%VERBOSE%"=="true" (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_VERBOSE_MAKEFILE=ON"
)

cmake -G "%GENERATOR%" %CMAKE_ARGS% ..
if errorlevel 1 (
    call :print_error "CMake configuration failed"
    popd
    popd
    exit /b 1
)

REM Build
call :print_status "Building OrcaSlicerCli (%BUILD_TYPE% with %JOBS% jobs)..."

if "%VERBOSE%"=="true" (
    cmake --build . --config %BUILD_TYPE% --parallel %JOBS% --verbose
) else (
    cmake --build . --config %BUILD_TYPE% --parallel %JOBS%
)

if errorlevel 1 (
    call :print_error "Build failed"
    popd
    popd
    exit /b 1
)

popd
popd
call :print_success "OrcaSlicerCli built successfully"
goto :eof

REM Function to install
:install_orcaslicer_cli
if "%DEPS_ONLY%"=="true" goto :eof

call :print_status "Installing OrcaSlicerCli to %INSTALL_DIR%..."

pushd "%BUILD_DIR%"
cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    call :print_error "Installation failed"
    popd
    exit /b 1
)

popd
call :print_success "OrcaSlicerCli installed successfully"
goto :eof

REM Main execution
:main
call :print_status "Starting OrcaSlicerCli build process..."
call :print_status "Build type: %BUILD_TYPE%"
call :print_status "Jobs: %JOBS%"
call :print_status "Generator: %GENERATOR%"
call :print_status "Project root: %PROJECT_ROOT%"
call :print_status "OrcaSlicer root: %ORCASLICER_ROOT%"

call :check_prerequisites
if errorlevel 1 exit /b 1

call :build_orcaslicer_deps
if errorlevel 1 exit /b 1

call :build_orcaslicer_cli
if errorlevel 1 exit /b 1

call :install_orcaslicer_cli
if errorlevel 1 exit /b 1

call :print_success "Build process completed successfully!"

if "%DEPS_ONLY%"=="false" (
    call :print_status "Executable location: %INSTALL_DIR%\bin\orcaslicer-cli.exe"
    call :print_status "Run '%INSTALL_DIR%\bin\orcaslicer-cli.exe --help' to get started"
)

goto :eof

REM Parse arguments and run main
call :parse_args %*
call :main
