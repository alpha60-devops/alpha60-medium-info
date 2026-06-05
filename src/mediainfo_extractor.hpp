#ifndef MEDIAINFO_EXTRACTOR_HPP
#define MEDIAINFO_EXTRACTOR_HPP

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <rapidjson/document.h>

namespace fs = std::filesystem;

struct VideoInfo {
    std::string codec;
    std::string codec_version;
    std::int64_t bitrate;
    int width;
    int height;
    int sampled_width;
    int sampled_height;
    std::string frame_rate;
};

struct AudioInfo {
    std::string codec;
    std::string codec_version;
    std::int64_t bitrate;
    int sampling_rate;
    std::string channels;
    int bit_depth;
    std::vector<std::string> languages;
};

struct SubtitleInfo {
    std::vector<std::string> languages;
    std::string format;
};

struct MediaInfoData {
    // Originating source
    std::string originating_source_medium_id;
    std::string originating_source_form;
    std::string originating_network_name;
    
    // Container
    std::string format_commercial_if_any;
    std::int64_t file_size;
    double duration;
    std::int64_t overall_bit_rate;
    std::string domain;
    std::string collection;
    std::string season;
    std::string distributed_by;
    std::string genre;
    std::string content_type;
    std::string owner;
    std::string country;
    std::string comment;
    std::string language;
    
    // Video
    VideoInfo video;
    
    // Audio
    AudioInfo audio;
    
    // Subtitle
    SubtitleInfo subtitle;
    
    std::string video_creation_metadata;
};

class MediaInfoExtractor {
public:
    explicit MediaInfoExtractor(const fs::path& media_file);
    
    std::optional<MediaInfoData> extract();
    
private:
    fs::path media_file_;
    
    std::string exec_mediainfo();
    bool parse_json_output(const std::string& json_output, MediaInfoData& data);
    std::string extract_string_value(const rapidjson::Value& obj, const char* key);
    std::int64_t extract_int64_value(const rapidjson::Value& obj, const char* key);
    double extract_double_value(const rapidjson::Value& obj, const char* key);
};

#endif // MEDIAINFO_EXTRACTOR_HPP