#include "torrent_parser.hpp"
#include "torrent_downloader.hpp"
#include "mediainfo_extractor.hpp"
#include "json_enricher.hpp"
#include <iostream>
#include <vector>
#include <filesystem>
#include <atomic>
#include <csignal>
#include <iomanip>

namespace fs = std::filesystem;
using namespace std;

// Global flag for interrupt handling
atomic<bool> g_interrupted{false};

static void
signal_handler(int /*sig*/)
{
  cout << "\n! Interrupted by user. Cleaning up..." << endl;
  g_interrupted = true;
}

static void
print_usage(const char* prog_name)
{
  cout << "Usage: " << prog_name << " <input_directory> [output_file] [cache_dir]" << endl;
  cout << endl;
  cout << "  input_directory: Directory containing .torrent files" << endl;
  cout << "  output_file:     Optional output JSON path (default: output/media_objects_content_analysis.json)" << endl;
  cout << "  cache_dir:       Optional directory for temporary downloads (default: ./download_cache)" << endl;
  cout << endl;
  cout << "Example:" << endl;
  cout << "  " << prog_name << " /path/to/collection-torrents-dir ./enriched.json ./cache" << endl;
}

// ============================================================
// 1. Parse torrents from input directory
// ============================================================
vector<TorrentFile>
parse_torrents(const fs::path& input_dir)
{
  cout << "\n[1/3] Parsing torrent files..." << endl;
  TorrentParser parser(input_dir);
  auto torrents = parser.parse_all_torrents();

  if (torrents.empty()) {
    cerr << "Error: No .torrent files found in " << input_dir << endl;
  } else {
    cout << "Found " << torrents.size() << " torrent file(s)" << endl;
  }

  return torrents;
}

// ============================================================
// 2. Download media sample for a single torrent
// ============================================================
struct download_result
{
  fs::path media_path;
  bool success;
  string error_msg;
};

download_result
download_torrent_media(const TorrentFile& tf,
                       const fs::path& cache_dir,
                       size_t mini_size)
{
  download_result result;
  result.success = false;

  // Create unique subdirectory for this torrent using its BTIH
  fs::path torrent_cache_dir = cache_dir / tf.btih;
  fs::create_directories(torrent_cache_dir);

  // Check if we already have a cached download
  fs::path cached_file = torrent_cache_dir / "media_sample";
  if (fs::exists(cached_file) && fs::file_size(cached_file) >= mini_size) {
    cout << "    Using cached download: " << cached_file << endl;
    result.media_path = cached_file;
    result.success = true;
    return result;
  }

  // Download minimal media file
  cout << "    Downloading first " << mini_size << "MB..." << endl;
  media_downloader downloader;
  auto media_path = downloader.download_minimal(tf.torrent_path.string(),
                                                torrent_cache_dir.string(),
                                                mini_size);

  if (!media_path.has_value()) {
    result.error_msg = "Failed to download media file";
    return result;
  }

  fs::path final_path = media_path.value();

  // Create symlink for cache consistency
  fs::path cache_link = torrent_cache_dir / "media_sample";
  if (!fs::exists(cache_link)) {
    try {
      fs::create_symlink(final_path, cache_link);
    } catch (...) {
      // Symlink failed, just use the original path
      cache_link = final_path;
    }
  }

  result.media_path = final_path;
  result.success = true;
  return result;
}

// ============================================================
// 3. Extract media info from downloaded file
// ============================================================
struct extract_result
{
  MediaInfoData data;
  bool success;
  string error_msg;
};

extract_result
extract_media_info(const fs::path& media_path)
{
  extract_result result;
  result.success = false;

  MediaInfoExtractor extractor(media_path);
  auto media_data = extractor.extract();

  if (!media_data.has_value()) {
    result.error_msg = "Failed to extract metadata";
    return result;
  }

  result.data = media_data.value();
  result.success = true;

  // Print brief summary
  const auto& md = result.data;
  cout << "    ✓ Codec: " << (md.video.codec_id.empty() ? "unknown" : md.video.codec_id);
  if (md.video.width > 0 && md.video.height > 0) {
    cout << ", Resolution: " << md.video.width << "x" << md.video.height;
  }
  if (!md.video.frame_rate.empty()) {
    cout << ", FPS: " << md.video.frame_rate;
  }
  cout << endl;

  return result;
}

// ============================================================
// 4. Process all torrents (download + extract)
// ============================================================
struct process_result
{
  vector<MediaInfoData> media_data_list;
  vector<fs::path> downloaded_files;
  size_t success_count;
  size_t fail_count;
};

process_result
process_all_torrents(const vector<TorrentFile>& torrents,
                     const fs::path& cache_dir,
                     size_t mini_size,
                     bool download_p = true)
{
  process_result result;
  result.success_count = 0;
  result.fail_count = 0;

  cout << "\n[2/3] " << (download_p ? "Downloading" : "Using cache only") << " media ..." << endl;

  for (size_t i = 0; i < torrents.size() && !g_interrupted; ++i) {
    const auto& tf = torrents[i];
    cout << "\n  [" << (i+1) << "/" << torrents.size() << "] " << tf.name << endl;

    fs::path torrent_cache_dir = cache_dir / tf.btih;
    fs::path cached_file = torrent_cache_dir / "media_sample";
    bool cache_exists = fs::exists(cached_file) && fs::file_size(cached_file) >= mini_size;

    // If cache exists, use it
    if (cache_exists) {
      cout << "    Using cached download: " << cached_file << endl;
      result.downloaded_files.push_back(cached_file);

      cout << "    Extracting metadata..." << endl;
      auto extract_result = extract_media_info(cached_file);
      if (extract_result.success) {
        result.media_data_list.push_back(extract_result.data);
        result.success_count++;
      } else {
        cerr << "    ✗ " << extract_result.error_msg << endl;
        result.media_data_list.push_back(MediaInfoData());
        result.fail_count++;
      }
      continue;
    }

    // No cache found
    if (!download_p) {
      cerr << "    ✗ No cache found and download disabled. Skipping." << endl;
      result.media_data_list.push_back(MediaInfoData());
      result.downloaded_files.push_back("");
      result.fail_count++;
      continue;
    }

    // Download
    auto download_result = download_torrent_media(tf, cache_dir, mini_size);
    if (!download_result.success) {
      cerr << "    ✗ " << download_result.error_msg << endl;
      result.media_data_list.push_back(MediaInfoData());
      result.downloaded_files.push_back("");
      result.fail_count++;
      continue;
    }

    result.downloaded_files.push_back(download_result.media_path);
    cout << "    ✓ Downloaded to: " << download_result.media_path << endl;

    // Extract metadata
    cout << "    Extracting metadata..." << endl;
    auto extract_result = extract_media_info(download_result.media_path);
    if (!extract_result.success) {
      cerr << "    ✗ " << extract_result.error_msg << endl;
      result.media_data_list.push_back(MediaInfoData());
      result.fail_count++;
      continue;
    }

    result.media_data_list.push_back(extract_result.data);
    result.success_count++;
  }

  return result;
}

// ============================================================
// 5. Write enriched JSON output
// ============================================================
bool
write_enriched_output(const fs::path& output_file,
                      const vector<TorrentFile>& torrents,
                      const vector<MediaInfoData>& media_data_list,
                      size_t mini_size)
{
  cout << "\n[3/3] Building enriched JSON..." << endl;

  JsonEnricher enricher;
  string json_output = enricher.build_output(torrents, media_data_list, mini_size);

  if (!enricher.write_output(output_file.string(), json_output)) {
    cerr << "✗ Error: Failed to write output file" << endl;
    return false;
  }

  cout << "✓ Successfully wrote enriched JSON to: " << output_file << endl;

  if (fs::exists(output_file)) {
    auto size = fs::file_size(output_file);
    cout << "  Output size: " << fixed << setprecision(2)
         << (size / 1024.0 / 1024.0) << " MB" << endl;
  }

  return true;
}

// ============================================================
// 6. Print final summary
// ============================================================
void
print_summary(size_t total_torrents, const process_result& process_result)
{
  cout << "\n========================================" << endl;
  cout << "  Pipeline Summary" << endl;
  cout << "========================================" << endl;
  cout << "  Total torrents:        " << total_torrents << endl;
  cout << "  Successfully processed: " << process_result.success_count << endl;
  cout << "  Failed:                 " << process_result.fail_count << endl;
  cout << "========================================" << endl;

  if (g_interrupted) {
    cout << "  Note: Interrupted by user" << endl;
  }
}

// ============================================================
// main()
// ============================================================
int main(int argc, char* argv[])
{
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  // Parse command line arguments
  fs::path input_dir = argv[1];
  fs::path output_file = (argc >= 3) ? argv[2] : "media_objects_medium_info.json";
  fs::path cache_dir = (argc >= 4) ? argv[3] : "download.cache";

  // Validate input directory
  if (!fs::exists(input_dir) || !fs::is_directory(input_dir)) {
    cerr << "Error: Input directory does not exist: " << input_dir << endl;
    return 1;
  }

  // Create output and cache directories
  if (output_file.has_parent_path()) {
    fs::create_directories(output_file.parent_path());
  }
  fs::create_directories(cache_dir);

  // Print banner
  cout << "========================================" << endl;
  cout << "  Media Enrichment Pipeline v1.0" << endl;
  cout << "========================================" << endl;
  cout << "Input directory:  " << input_dir << endl;
  cout << "Output file:      " << output_file << endl;
  cout << "Cache directory:  " << cache_dir << endl;
  cout << "========================================" << endl;

  // Step 1: Parse torrents
  auto torrents = parse_torrents(input_dir);
  if (torrents.empty()) {
    return 1;
  }

  // Step 2: Process all torrents (download + extract)
  const size_t mini_size = 16 * 1024 * 1024;  // 16 MB
  bool download_p = true;  // Set to false to skip downloads, use cache only
  auto process_result = process_all_torrents(torrents, cache_dir, mini_size, download_p);

  // Check for interrupt
  if (g_interrupted) {
    cout << "\n! Interrupted. Cleaning up..." << endl;
    return 130;
  }

  // Step 3: Write output
  if (!write_enriched_output(output_file, torrents, process_result.media_data_list, mini_size)) {
    return 1;
  }

  // Print summary
  print_summary(torrents.size(), process_result);

  cout << "\n========================================" << endl;
  cout << "  Pipeline completed successfully!" << endl;
  cout << "========================================" << endl;

  return 0;
}