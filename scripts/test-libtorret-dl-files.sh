# Find the downloaded file
find download_cache -name "media_sample" -type f

# Run mediainfo on it directly
mediainfo download_cache/1d0262a0f4880be8bc8352422836596bc457d03b/media_sample

# Check if it's a valid media file
file download_cache/1d0262a0f4880be8bc8352422836596bc457d03b/media_sample

# Check the first few bytes (should be a valid container header)
xxd download_cache/1d0262a0f4880be8bc8352422836596bc457d03b/media_sample | head -20