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

class JsonEnricher {
public:
    JsonEnricher();
    
    std::string build_output(const std::vector<TorrentFile>& torrents,
                             const std::vector<MediaInfoData>& media_data);
    
    bool write_output(const std::string& output_path, const std::string& json_content);
    
private:
    std::string json_string(const std::string& str);
    std::string escape_json_string(const std::string& str);
};

#endif // JSON_ENRICHER_HPP
