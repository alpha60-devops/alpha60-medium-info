
## docs/architecture_overview.md

```markdown
# MediaInfo Enrichment Pipeline - Architecture Overview

## System Overview

The MediaInfo Enrichment Pipeline is a batch processing system that analyzes torrent files and their corresponding media content to produce enriched JSON metadata. The system runs on Fedora 43 x86_64 and uses C++20, libtorrent, rapidjson, and the MediaInfo binary.

## Design Principles

1. **Stateless Processing**: Each run processes a directory of torrent files independently
2. **Idempotent Output**: Running the same input multiple times produces identical output
3. **Fail Gracefully**: Missing media files or extraction errors result in null fields, not pipeline failure
4. **Performance**: Multi-threading support (extensible) for processing large batches

## Core Components

### 1. Torrent Parser (`torrent_parser.cpp/hpp`)
- **Responsibility**: Parse `.torrent` files and extract metadata
- **Dependencies**: libtorrent-rasterbar
- **Input**: Directory path containing `.torrent` files
- **Output**: Vector of `TorrentFile` structures containing BTIH, name, file paths, sizes

### 2. MediaInfo Extractor (`mediainfo_extractor.cpp/hpp`)
- **Responsibility**: Execute MediaInfo binary and parse JSON output
- **Dependencies**: mediainfo binary, rapidjson
- **Input**: Filesystem path to media file (.mkv, .mp4, etc.)
- **Output**: `MediaInfoData` structure with all technical fields

### 3. JSON Enricher (`json_enricher.cpp/hpp`)
- **Responsibility**: Combine torrent metadata with MediaInfo data into final JSON
- **Dependencies**: rapidjson, standard library
- **Input**: Vectors of TorrentFile and MediaInfoData
- **Output**: JSON string conforming to API specification

### 4. Main Driver (`main.cpp`)
- **Responsibility**: Orchestrate the pipeline
- **Input**: Command-line arguments (input directory, optional output path)
- **Output**: Enriched JSON file on disk

## Data Flow
