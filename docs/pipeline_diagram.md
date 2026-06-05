# MediaInfo Enrichment Pipeline Diagram

## System Context Diagram

```mermaid
C4Context
    title System Context diagram for MediaInfo Enrichment Pipeline

    Person(user, "Operator", "Runs the enrichment pipeline")
    System(pipeline, "MediaInfo Enrichment Pipeline", "Analyzes torrent files and media content")
    System_Ext(fs, "Filesystem", "Stores .torrent and media files")
    System_Ext(mi, "MediaInfo", "Extracts media technical metadata")

    Rel(user, pipeline, "Runs with input directory")
    Rel(pipeline, fs, "Reads .torrent and media files")
    Rel(pipeline, mi, "Executes and parses JSON output")
    Rel(pipeline, fs, "Writes enriched JSON output")