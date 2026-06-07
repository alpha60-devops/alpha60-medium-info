#!/bin/bash
# Media Enrichment Pipeline Runner
# Usage: ./run_enrichment.sh /path/to/torrent/dir /path/to/output.json

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check dependencies
check_dependencies() {
    local deps=("mediainfo" "cmake" "g++" "pkg-config")
    local missing=()

    for dep in "${deps[@]}"; do
	if ! command -v "$dep" &> /dev/null; then
	    missing+=("$dep")
	fi
    done

    if [ ${#missing[@]} -ne 0 ]; then
	log_error "Missing dependencies: ${missing[*]}"
	log_info "Install with: sudo dnf install -y mediainfo cmake gcc-c++ pkgconfig"
	exit 1
    fi

    # Check for libtorrent development files

    # Check for rapidjson
}

# Build the project
build_project() {
    log_info "Building project..."

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    cmake .. -DCMAKE_BUILD_TYPE=Release
    gmake -j$(nproc)

    cd "${PROJECT_DIR}"
    log_info "Build complete"
}

# Run the enrichment
run_enrichment() {
    local input_dir="$1"
    local output_file="$2"
    local cache_dir="$3"

    if [ ! -d "${input_dir}" ]; then
	log_error "Input directory does not exist: ${input_dir}"
	exit 1
    fi

    log_info "Input directory: ${input_dir}"
    log_info "Output file: ${output_file}"
    log_info "Cache directory: ${cache_dir}"    

    "${BUILD_DIR}/media_enrichment" "${input_dir}" "${output_file}" "${cache_dir}"

    if [ $? -eq 0 ]; then
	log_info "Enrichment completed successfully"
    else
	log_error "Enrichment failed"
	exit 1
    fi
}

# Main execution
main() {
    if [ $# -lt 1 ]; then
	echo "Usage: $0 <input_directory> [output_file]"
	echo "  input_directory: Directory containing .torrent files"
	echo "  output_file: Optional output JSON path (default: ./output/media_objects_content_analysis.json)"
	exit 1
    fi

    pwdir=`pwd`
    local input_dir="$1"
    local output_file="${2:-media_objects_content_analysis.json}"
    local cache_dir="${3:-downloads.cache}"

    log_info "Starting Media Enrichment Pipeline"
    log_info "Fedora 43 x86_64 Environment"
    echo "pwd: ${pwdir}"

    check_dependencies
    build_project

    run_enrichment "${input_dir}" "${pwdir}/${output_file}" "${pwdir}/${cache_dir}"

    log_info "Pipeline complete"
}

main "$@"
