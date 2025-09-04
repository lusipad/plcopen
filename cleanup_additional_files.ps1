# Additional cleanup script for remaining build-related files
# This script removes CMake files and build scripts that are no longer needed
# after switching to direct compilation approach

Write-Host "Cleaning up additional build-related files..." -ForegroundColor Yellow

# Files to remove
$FilesToRemove = @(
    "CMakeLists.txt",
    "CMakeLists_simple_test.txt", 
    "build.ps1",
    "cleanup_invalid_files.ps1"
)

# Remove files
foreach ($file in $FilesToRemove) {
    if (Test-Path $file) {
        Remove-Item $file -Force
        Write-Host "Removed: $file" -ForegroundColor Green
    } else {
        Write-Host "File not found: $file" -ForegroundColor Gray
    }
}

# Check for any remaining CMake-related files in subdirectories
$CMakeFiles = Get-ChildItem -Recurse -Name "CMakeLists.txt", "*.cmake", "CMakeSettings.json"
if ($CMakeFiles) {
    Write-Host "\nFound additional CMake files:" -ForegroundColor Yellow
    foreach ($file in $CMakeFiles) {
        Write-Host "  $file" -ForegroundColor Cyan
        $response = Read-Host "Remove this file? (y/N)"
        if ($response -eq 'y' -or $response -eq 'Y') {
            Remove-Item $file -Force
            Write-Host "  Removed: $file" -ForegroundColor Green
        }
    }
}

Write-Host "\nAdditional cleanup completed!" -ForegroundColor Green
Write-Host "\nRemaining project structure is now optimized for direct compilation." -ForegroundColor Cyan