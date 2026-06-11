#ifndef JSON_ENRICHER_HPP
#define JSON_ENRICHER_HPP

#include "torrent_parser.hpp"
#include "mediainfo_extractor.hpp"

#include <string>
#include <vector>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>

namespace fs = std::filesystem;

struct pipeline_metrics
{
  size_t btiha_size = 0;              // total number of torrents processed
  size_t media_cache_file_size = 0;   // size of each cached file in bytes
  size_t btiha_unreachable_size = 0;  // cache file_size == 0
  size_t btiha_partial_size = 0;      // cache file_size > 0, but incomplete
  size_t btiha_extracted_size = 0;    // basic video/audio metadata confirmed
  double btiha_extracted_percent = 0.0;
};

class enrichment
{
public:
    enrichment();

    std::string
    build_output(const std::vector<TorrentFile>& torrents,
		 const std::vector<MediaInfoData>& media_data,
		 const std::vector<fs::path>& downloaded_files,
		 const std::string& collection_key,
		 const uint mini_size,
		 uintmax_t cache_dir_size_mb,
		 uintmax_t torrent_total_size_mb);

    bool
    write_output(const std::string& output_path, const std::string& json_content);

private:
    pipeline_metrics
    compute_pipeline_metrics(const std::vector<fs::path>& downloaded_files,
			     const std::vector<MediaInfoData>& media_data,
			     size_t mini_size);

    std::string json_string(const std::string& str);
    std::string escape_json_string(const std::string& str);
};

#endif // JSON_ENRICHER_HPP
