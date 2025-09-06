#!/bin/bash
# Code Coverage Report Generator
# PLCOpen Project Unit Test Enhancement

set -e

# Configuration
BUILD_DIR="build"
COVERAGE_DIR="build/coverage"
HTML_REPORT_DIR="build/coverage/html"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== PLCOpen Unit Test Coverage Report Generator ===${NC}"
echo -e "Build directory: ${BUILD_DIR}"
echo -e "Coverage directory: ${COVERAGE_DIR}"
echo

# Check if gcov and lcov are available
if ! command -v gcov &> /dev/null; then
    echo -e "${RED}Error: gcov not found. Please install gcc with gcov support.${NC}"
    exit 1
fi

if ! command -v lcov &> /dev/null; then
    echo -e "${RED}Error: lcov not found. Please install lcov for coverage reporting.${NC}"
    echo -e "${YELLOW}Install with: sudo apt-get install lcov (Ubuntu) or brew install lcov (macOS)${NC}"
    exit 1
fi

# Create coverage directory
mkdir -p "${COVERAGE_DIR}"
mkdir -p "${HTML_REPORT_DIR}"

# Clean previous coverage data
echo -e "${YELLOW}Cleaning previous coverage data...${NC}"
find "${BUILD_DIR}" -name "*.gcda" -delete 2>/dev/null || true
find "${BUILD_DIR}" -name "*.gcno" -delete 2>/dev/null || true

# Configure and build with coverage flags
echo -e "${YELLOW}Building with coverage support...${NC}"
cd "${BUILD_DIR}"
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..
make clean
make -j$(nproc)

# Run unit tests
echo -e "${YELLOW}Running unit tests...${NC}"
cd ..
./build/tests/unit/run_unit_tests || echo -e "${RED}Some tests failed, continuing with coverage...${NC}"

# Generate coverage data
echo -e "${YELLOW}Generating coverage data...${NC}"
cd "${BUILD_DIR}"

# Create baseline coverage data
lcov --directory . --zerocounters
lcov --directory . --capture --initial --output-file "${COVERAGE_DIR}/baseline.info"

# Capture coverage data after test execution  
lcov --directory . --capture --output-file "${COVERAGE_DIR}/test.info"

# Combine baseline and test data
lcov --add-tracefile "${COVERAGE_DIR}/baseline.info" --add-tracefile "${COVERAGE_DIR}/test.info" --output-file "${COVERAGE_DIR}/combined.info"

# Filter out system headers and test files
lcov --remove "${COVERAGE_DIR}/combined.info" '/usr/*' '*/tests/*' '*/build/*' --output-file "${COVERAGE_DIR}/filtered.info"

# Generate HTML report
echo -e "${YELLOW}Generating HTML coverage report...${NC}"
genhtml "${COVERAGE_DIR}/filtered.info" --output-directory "${HTML_REPORT_DIR}" --title "PLCOpen Unit Test Coverage" --num-spaces 4 --sort --function-coverage --branch-coverage

# Generate text summary
lcov --summary "${COVERAGE_DIR}/filtered.info" > "${COVERAGE_DIR}/summary.txt"

# Display summary
echo -e "${GREEN}=== Coverage Summary ===${NC}"
cat "${COVERAGE_DIR}/summary.txt"

# Extract coverage percentage
COVERAGE_PERCENT=$(grep "lines" "${COVERAGE_DIR}/summary.txt" | grep -o '[0-9]\+\.[0-9]\+%' | head -1)

echo
echo -e "${BLUE}Coverage report generated successfully!${NC}"
echo -e "HTML report available at: ${GREEN}${HTML_REPORT_DIR}/index.html${NC}"
echo -e "Overall line coverage: ${GREEN}${COVERAGE_PERCENT}${NC}"

# Check coverage threshold
THRESHOLD=85
COVERAGE_NUM=$(echo $COVERAGE_PERCENT | sed 's/%//')
if (( $(echo "$COVERAGE_NUM >= $THRESHOLD" | bc -l) )); then
    echo -e "${GREEN}✓ Coverage meets threshold (${THRESHOLD}%)${NC}"
    exit 0
else
    echo -e "${RED}✗ Coverage below threshold (${THRESHOLD}%). Current: ${COVERAGE_PERCENT}${NC}"
    exit 1
fi