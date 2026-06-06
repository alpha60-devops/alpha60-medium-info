Alpha60 Medium Enrichment Pipeline Documentation

Welcome to the documentation for the **MediaInfo Enrichment Pipeline**. This tool is a batch processing system that parses BitTorrent .torrent files, downloads a minimal portion of their associated media content, extracts detailed technical metadata using mediainfo, and outputs a structured JSON report.

Documentation Sections

- API Specifications (api_specifications.md) – The schema for the final JSON output.
- Architecture Overview (architecture_overview.md) – A high-level description of the system's components and design.
- Pipeline Diagrams (pipeline_diagram.md) – Visual representations of the system's flow and deployment.

Quick Start (tl;dr)

# Build the project
cd alpha60-medium-info
mkdir build && cd build
cmake .. && make -j$(nproc)

# Run the pipeline
./media_enrichment /path/to/torrent/dir ./output.json ./cache_dir

Key Features

- Minimal Downloading: Only downloads the first 10MB of the media file—enough to extract all technical metadata.
- Persistent Cache: Stores downloaded partial files to avoid re-downloading on subsequent runs.
- Comprehensive Metadata: Extracts codecs, resolution, bitrate, audio languages, and more.
- Robust API: Outputs a strictly versioned JSON schema (1.0).

Visuals

The architecture and workflow are illustrated using Mermaid and SVG diagrams found in this directory:

- build-runtime-flow.svg: Build and runtime flow.
- component-sequence-diagram.svg: Sequence diagram of the main pipeline.
- container-diagram.svg: C4 container diagram.
- data-structure-diagram.svg: Class/struct diagram of core data models.
- deployment-diagram.svg: Deployment layout on Fedora.
- legend.svg: Legend for the diagrams.
