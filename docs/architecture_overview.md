# MediaInfo Enrichment Pipeline - Architecture Overview

## System Overview

The MediaInfo Enrichment Pipeline is a batch processing system that analyzes torrent files and their corresponding media content to produce enriched JSON metadata. The system runs on Fedora 43 x86_64 and uses C++17, libtorrent 2.0.11, rapidjson, and the MediaInfo binary.

## Design Principles

1.  **Stateless Processing**: Each run processes a directory of torrent files independently.
2.  **Idempotent Output**: Running the same input multiple times produces identical output.
3.  **Fail Gracefully**: Missing media files or extraction errors result in null fields, not pipeline failure.
4.  **Minimal Download**: Only downloads the first 10MB of a media file, which is sufficient for header analysis and metadata extraction.

## Core Components

### 1. Torrent Parser (`torrent_parser.cpp/hpp`)
- **Responsibility**: Parse `.torrent` files and extract metadata (BTIH, file names, sizes) without downloading.
- **Dependencies**: `libtorrent-rasterbar`, OpenSSL.
- **Input**: Directory path containing `.torrent` files.
- **Output**: Vector of `TorrentFile` structures.

### 2. Torrent Downloader (`torrent_downloader.cpp/hpp`)
- **Responsibility**: Download only the first N bytes (default 10MB) of the first file in a torrent, enabling `set_sequential_download(true)` to prioritize headers.
- **Dependencies**: `libtorrent-rasterbar`.
- **Input**: Path to a `.torrent` file and an output cache directory.
- **Output**: Filesystem path to the downloaded partial file.

### 3. MediaInfo Extractor (`mediainfo_extractor.cpp/hpp`)
- **Responsibility**: Execute the `mediainfo` binary, parse its JSON output, and map the fields to the internal `MediaInfoData` structure.
- **Dependencies**: `mediainfo` binary, `rapidjson`.
- **Input**: Filesystem path to a media file.
- **Output**: `MediaInfoData` structure.

### 4. JSON Enricher (`json_enricher.cpp/hpp`)
- **Responsibility**: Combine a `TorrentFile` with its corresponding `MediaInfoData` and serialize it into the final API-specification JSON.
- **Dependencies**: `rapidjson`.
- **Input**: Vectors of `TorrentFile` and `MediaInfoData`.
- **Output**: JSON string conforming to `api_specifications.md`.

### 5. Main Driver (`main.cpp`)
- **Responsibility**: Orchestrate the pipeline steps, manage the cache directory, handle signals, and output results.
- **Input**: Command-line arguments (input directory, optional output path, optional cache directory).
- **Output**: Enriched JSON file on disk.

## Technology Stack

| Component | Technology | Version | Purpose |
| :--- | :--- | :--- | :--- |
| Language | C++ | 17 (with some 20 features) | Core implementation |
| Build System | CMake | 3.20+ | Build configuration |
| Torrent Parsing | libtorrent-rasterbar | 2.0.11 | Parse `.torrent` files, download media |
| JSON Parsing | rapidjson | 1.1+ | Parse MediaInfo JSON output |
| Media Analysis | mediainfo | 23.0+ | Extract media technical metadata |
| Crypto | OpenSSL | 3.0+ | SHA1 computation for BTIH |
| Platform | Fedora | 43 x86_64 | Target OS |

## Error Handling Strategy

| Scenario | Handling |
| :--- | :--- |
| Missing `.torrent` file | Skip, continue processing. |
| Corrupt `.torrent` file | Log warning, skip. |
| Media download fails (no peers) | All media fields remain `null`. |
| MediaInfo extraction fails | All MediaInfo fields are `null`. |
| JSON parsing error | Log error, skip file. |

## Performance Considerations

- **Memory**: Loads one torrent at a time; peak memory ~50MB.
- **Disk I/O**: Reads each `.torrent` file once; the downloader reads and writes only the first 10MB of media files.
- **CPU**: MediaInfo parsing is the dominant cost; scales linearly with file count.
- **Network**: Only connects to the BitTorrent network when a file is not in the cache.

## Security Considerations

- All file paths are validated before access.
- The `mediainfo` binary is executed in a controlled manner with escaped paths.
- The libtorrent session is configured with standard settings, no root privileges required.