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

// Global flag for interrupt handling
std::atomic<bool> g_interrupted{false};

static void signal_handler(int /*sig*/) {
    std::cout << "\n! Interrupted by user. Cleaning up..." << std::endl;
    g_interrupted = true;
}

static void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <input_directory> [output_file] [cache_dir]" << std::endl;
    std::cout << std::endl;
    std::cout << "  input_directory: Directory containing .torrent files" << std::endl;
    std::cout << "  output_file:     Optional output JSON path (default: output/media_objects_content_analysis.json)" << std::endl;
    std::cout << "  cache_dir:       Optional directory for temporary downloads (default: ./download_cache)" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << prog_name << " /path/to/torrents ./enriched.json ./cache" << std::endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (argc < 2) {
	print_usage(argv[0]);
	return 1;
    }

    fs::path input_dir = argv[1];
    fs::path output_file = (argc >= 3) ? argv[2] : "media_objects_medium_info.json";
    fs::path cache_dir = (argc >= 4) ? argv[3] : "download.cache";

    // Validate input directory
    if (!fs::exists(input_dir) || !fs::is_directory(input_dir)) {
	std::cerr << "Error: Input directory does not exist: " << input_dir << std::endl;
	return 1;
    }

    // Create output directory if needed
    if (output_file.has_parent_path()) {
	fs::create_directories(output_file.parent_path());
    }

    // Create cache directory
    fs::create_directories(cache_dir);

    std::cout << "========================================" << std::endl;
    std::cout << "  Media Enrichment Pipeline v1.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Input directory:  " << input_dir << std::endl;
    std::cout << "Output file:      " << output_file << std::endl;
    std::cout << "Cache directory:  " << cache_dir << std::endl;
    std::cout << "========================================" << std::endl;

    // Parse all torrents
    std::cout << "\n[1/3] Parsing torrent files..." << std::endl;
    TorrentParser parser(input_dir);
    auto torrents = parser.parse_all_torrents();

    std::cout << "Found " << torrents.size() << " torrent file(s)" << std::endl;

    if (torrents.empty()) {
	std::cerr << "Error: No .torrent files found in " << input_dir << std::endl;
	return 1;
    }

    // Initialize downloader
    std::cout << "\n[2/3] Downloading media metadata..." << std::endl;
    media_downloader downloader;

    // For each torrent, download minimal media file and extract metadata
    std::vector<MediaInfoData> media_data_list;
    std::vector<fs::path> downloaded_files;

    for (size_t i = 0; i < torrents.size() && !g_interrupted; ++i) {
	const auto& tf = torrents[i];
	std::cout << "\n  [" << (i+1) << "/" << torrents.size() << "] " << tf.name << std::endl;

	// Create a unique subdirectory for this torrent using its BTIH
	fs::path torrent_cache_dir = cache_dir / tf.btih;
	fs::create_directories(torrent_cache_dir);

	// Check if we already have a cached download
	fs::path cached_file = torrent_cache_dir / "media_sample";
	if (fs::exists(cached_file) && fs::file_size(cached_file) >= 10 * 1024 * 1024) {
	    std::cout << "    Using cached download: " << cached_file << std::endl;
	    downloaded_files.push_back(cached_file);

	    // Extract MediaInfo from cached file
	    MediaInfoExtractor extractor(cached_file);
	    auto media_data = extractor.extract();

	    if (media_data.has_value()) {
		media_data_list.push_back(media_data.value());
		std::cout << "    ✓ Extracted metadata from cache" << std::endl;
	    } else {
		std::cerr << "    ✗ Failed to extract metadata from cache" << std::endl;
		media_data_list.push_back(MediaInfoData());
	    }
	    continue;
	}

	// Download minimal media file
	std::cout << "    Downloading first 10MB..." << std::endl;
	auto media_path = downloader.download_minimal(
	    tf.torrent_path.string(),
	    torrent_cache_dir.string(),
	    10 * 1024 * 1024  // 10 MB
	);

	if (!media_path.has_value()) {
	    std::cerr << "    ✗ Failed to download media file" << std::endl;
	    media_data_list.push_back(MediaInfoData());
	    downloaded_files.push_back("");
	    continue;
	}

	// media_path is already the final file path (e.g., .../ubuntu-22.04.5-desktop-amd64.iso)
	// No need to rename - just use it directly
	fs::path final_path = media_path.value();

	// Create a symlink or copy to media_sample for cache consistency
	fs::path cache_link = torrent_cache_dir / "media_sample";
	if (!fs::exists(cache_link)) {
	    try {
		fs::create_symlink(final_path, cache_link);
	    } catch (...) {
		// Symlink failed, just use the original path
		cache_link = final_path;
	    }
	}

	downloaded_files.push_back(final_path);
	std::cout << "    ✓ Downloaded to: " << final_path << std::endl;

	// Extract MediaInfo
	std::cout << "    Extracting metadata..." << std::endl;
	MediaInfoExtractor extractor(final_path);
	auto media_data = extractor.extract();

	if (media_data.has_value()) {
	    media_data_list.push_back(media_data.value());

	    // Print brief summary of what we found
	    const auto& md = media_data.value();
	    std::cout << "    ✓ Codec: " << (md.video.codec_id.empty() ? "unknown" : md.video.codec_id);
	    if (md.video.width > 0 && md.video.height > 0) {
		std::cout << ", Resolution: " << md.video.width << "x" << md.video.height;
	    }
	    if (!md.video.frame_rate.empty()) {
		std::cout << ", FPS: " << md.video.frame_rate;
	    }
	    std::cout << std::endl;

	} else {
	    std::cerr << "    ✗ Failed to extract metadata" << std::endl;
	    media_data_list.push_back(MediaInfoData());
	}
    }

    if (g_interrupted) {
	std::cout << "\n! Interrupted. Cleaning up..." << std::endl;
	return 130;
    }

    // Build enriched JSON
    std::cout << "\n[3/3] Building enriched JSON..." << std::endl;
    JsonEnricher enricher;
    std::string json_output = enricher.build_output(torrents, media_data_list);

    // Write output file
    if (enricher.write_output(output_file.string(), json_output)) {
	std::cout << "✓ Successfully wrote enriched JSON to: " << output_file << std::endl;

	// Print file size
	if (fs::exists(output_file)) {
	    auto size = fs::file_size(output_file);
	    std::cout << "  Output size: " << std::fixed << std::setprecision(2)
		      << (size / 1024.0 / 1024.0) << " MB" << std::endl;
	}
    } else {
	std::cerr << "✗ Error: Failed to write output file" << std::endl;
	return 1;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Pipeline completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
