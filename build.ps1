#!/usr/bin/env pwsh

[CmdletBinding()]
param(
    [Parameter()]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    
    [Parameter()]
    [switch]$Clean,
    
    [Parameter()]
    [switch]$Test,
    
    [Parameter()]
    [switch]$Install
)

# Set error handling
$ErrorActionPreference = "Stop"

# Color output functions
function Write-ColorOutput {
    param([string]$Message, [string]$Color = "White")
    Write-Host $Message -ForegroundColor $Color
}

function Write-Success { param([string]$Message) Write-ColorOutput "✅ $Message" "Green" }
function Write-Info { param([string]$Message) Write-ColorOutput "ℹ️  $Message" "Cyan" }
function Write-Warning { param([string]$Message) Write-ColorOutput "⚠️  $Message" "Yellow" }
function Write-Error { param([string]$Message) Write-ColorOutput "❌ $Message" "Red" }

# Global variables
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = $ScriptDir
$BuildDir = Join-Path $ProjectRoot "build"
$OutDir = Join-Path $ProjectRoot "out"
$StartTime = Get-Date

# Environment check function
function Test-Environment {
    Write-Info "Checking build environment..."
    
    # PowerShell version check
    Write-Success "PowerShell version: $($PSVersionTable.PSVersion)"
    
    # Operating system check
    Write-Success "Operating system: $env:OS"
    
    # CMake check
    $CMakePath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($CMakePath) {
        $CMakeVersion = & cmake --version | Select-Object -First 1
        Write-Success "CMake: $CMakeVersion"
    } else {
        throw "CMake not found, please install CMake 3.21 or higher"
    }
}

# Clean function
function Clear-Build {
    Write-Info "Cleaning build directory..."
    
    if (Test-Path $BuildDir) {
        Remove-Item $BuildDir -Recurse -Force
        Write-Success "Build directory cleaned"
    }
    
    if (Test-Path $OutDir) {
        Remove-Item $OutDir -Recurse -Force
        Write-Success "Output directory cleaned"
    }
}

# Configure CMake function
function Invoke-CMakeConfigure {
    Write-Info "Configuring CMake project..."
    
    # Create build directory
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }
    
    # Set CMake variables
    $CMakeVars = @(
        "-G", "Visual Studio 17 2022",
        "-DCMAKE_BUILD_TYPE=$Configuration"
    )
    
    # Execute CMake configuration
    $CMakeArgs = $CMakeVars + $ProjectRoot
    Write-Info "CMake command: cmake $($CMakeArgs -join ' ')"
    
    Push-Location $BuildDir
    try {
        $Result = & cmake @CMakeArgs 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configuration failed: $Result"
        }
        Write-Success "CMake configuration successful"
    } finally {
        Pop-Location
    }
}

# Build function
function Invoke-CMakeBuild {
    Write-Info "Building project (Configuration: $Configuration)..."
    
    $BuildArgs = @(
        "--build", $BuildDir,
        "--config", $Configuration,
        "--parallel"
    )
    
    Write-Info "Build command: cmake $($BuildArgs -join ' ')"
    
    $Result = & cmake @BuildArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $Result"
    }
    
    Write-Success "Build successful"
}

# Test function
function Invoke-Tests {
    if (-not $Test) {
        return
    }
    
    Write-Info "Executing test suite..."
    
    # Check test executable
    $TestExe = Join-Path $BuildDir $Configuration "test_basic.exe"
    if (-not (Test-Path $TestExe)) {
        Write-Warning "Test executable not found: $TestExe"
        return
    }
    
    # Execute tests
    Write-Info "Running tests: $TestExe"
    $TestResult = & $TestExe 2>&1
    $TestExitCode = $LASTEXITCODE
    
    if ($TestExitCode -eq 0) {
        Write-Success "All tests passed"
    } else {
        Write-Error "Tests failed (exit code: $TestExitCode)"
        Write-Output $TestResult
    }
}

# Install function
function Invoke-Install {
    if (-not $Install) {
        return
    }
    
    Write-Info "Installing project to: $OutDir"
    
    # Create output directory
    if (-not (Test-Path $OutDir)) {
        New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    }
    
    # Copy executables
    $SourceDir = Join-Path $BuildDir $Configuration
    if (Test-Path $SourceDir) {
        Copy-Item "$SourceDir\*.exe" $OutDir -Force
        Copy-Item "$SourceDir\*.dll" $OutDir -Force
        Copy-Item "$SourceDir\*.lib" $OutDir -Force
        Write-Success "Installation successful"
        
        # Show installation contents
        Write-Info "Installation contents:"
        Get-ChildItem $OutDir | ForEach-Object {
            Write-Info "  $($_.Name)"
        }
    } else {
        Write-Warning "Source directory not found: $SourceDir"
    }
}

# Main function
function Main {
    try {
        Write-ColorOutput "🚀 Uranus PLC Build Script Starting" "Magenta"
        Write-ColorOutput "==========================================" "Magenta"
        
        # Environment check
        Test-Environment
        
        # Clean build directory
        if ($Clean) {
            Clear-Build
        }
        
        # CMake configuration
        Invoke-CMakeConfigure
        
        # Build project
        Invoke-CMakeBuild
        
        # Execute tests
        Invoke-Tests
        
        # Install project
        Invoke-Install
        
        Write-ColorOutput "==========================================" "Magenta"
        Write-Success "🎉 Build completed!"
        Write-Info "Build Configuration: $Configuration"
        Write-Info "Build Duration: $((Get-Date) - $StartTime).TotalSeconds.ToString('F2') seconds"
        
        if ($Test) {
            Write-Info "Test Status: Executed"
        }
        if ($Install) {
            Write-Info "Installation Location: $OutDir"
        }
        
    } catch {
        Write-Error "Build failed: $($_.Exception.Message)"
        exit 1
    }
}

# Execute main function
Main
