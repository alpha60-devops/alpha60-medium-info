# Alpha60 Swarm Medium Enrichment Pipeline Documentation

## Intro

Welcome to the documentation for the **Swarm Medium Info Probing
Pipeline**. This tool is a batch processing system that parses
BitTorrent .torrent files, downloads a minimal portion of their
associated media content, extracts detailed technical metadata using
mediainfo, and outputs a structured JSON report.

Naturally, we call it SMIPP. Use like: take a sip of the smipp!

## Documentation Sections

- [API Specifications](/docs/api_specifications.md) – The schema for the final JSON output.
- [Architecture Overview](/docs/architecture_overview.md) – A high-level description of the system's components and design.
- [Pipeline Diagrams](/docs/pipeline_diagram.md) – Visual representations of the system's flow and deployment.

## Quick Start (tl;dr)

#### Build the project
cd alpha60-medium-info
mkdir build && cd build
cmake .. && make -j$(nproc)

#### Run the pipeline
./media_enrichment /path/to/torrent/dir ./output.json ./cache_dir

## Key Features

- Minimal Downloading: Only downloads the first 10MB of the media file—enough to extract all technical metadata.
- Persistent Cache: Stores downloaded partial files to avoid re-downloading on subsequent runs.
- Comprehensive Metadata: Extracts codecs, resolution, bitrate, audio languages, and more.
- Robust API: Outputs a strictly versioned JSON schema (1.0).

## Visuals

The architecture and workflow are illustrated using Mermaid and SVG diagrams found in this directory:

- [Build and runtime flow](/docs/build-runtime-flow.svg)
- [Sequence diagram of the main pipeline](/docs/component-sequence-diagram.svg)
- [Container diagram](/docs/container-diagram.svg)
- [Core data models](/docs/data-structure-diagram.svg)
- [Deployment workflow](/docs/deployment-diagram.svg)
- [Legend](/docs/legend.svg)
