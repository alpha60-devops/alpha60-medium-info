#include "mediainfo_extractor.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <cstdlib>
#include <array>
#include <memory>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <iostream>

MediaInfoExtractor::MediaInfoExtractor(const fs::path& media_file)
    : media_file_(media_file) {}

std::optional<MediaInfoData> MediaInfoExtractor::extract() {
    if (media_file_.empty() || !fs::exists(media_file_)) {
        std::cerr << "  [MediaInfo] File not found: " << media_file_.string() << std::endl;
        return std::nullopt;
    }
    
    auto file_size = fs::file_size(media_file_);
    std::cout << "  [MediaInfo] Analyzing: " << media_file_.filename().string() 
              << " (" << file_size / (1024*1024) << " MB)" << std::endl;
    
    std::string json_output = exec_mediainfo();
    if (json_output.empty()) {
        std::cerr << "  [MediaInfo] No output from mediainfo" << std::endl;
        return std::nullopt;
    }
    
    MediaInfoData data;
    if (!parse_json_output(json_output, data)) {
        std::cerr << "  [MediaInfo] Failed to parse JSON output" << std::endl;
        return std::nullopt;
    }
    
    return data;
}

std::string MediaInfoExtractor::exec_mediainfo() {
    std::string cmd = std::string(MEDIAINFO_PATH) + " --Output=JSON \"" + media_file_.string() + "\" 2>/dev/null";
    std::array<char, 256> buffer;
    std::string result;
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        std::cerr << "  [MediaInfo] Failed to execute mediainfo" << std::endl;
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return result;
}

bool MediaInfoExtractor::parse_json_output(const std::string& json_output, MediaInfoData& data) {
    rapidjson::Document doc;
    if (doc.Parse(json_output.c_str()).HasParseError()) {
        std::cerr << "  [MediaInfo] JSON parse error at offset " << doc.GetErrorOffset() << std::endl;
        return false;
    }
    
    if (!doc.HasMember("media") || !doc["media"].IsObject()) {
        std::cerr << "  [MediaInfo] JSON missing 'media' object" << std::endl;
        return false;
    }
    
    const auto& media = doc["media"];
    if (!media.HasMember("track") || !media["track"].IsArray()) {
        std::cerr << "  [MediaInfo] JSON missing 'track' array" << std::endl;
        return false;
    }
    
    const auto& tracks = media["track"];
    
    // Initialize defaults
    data.file_size = 0;
    data.duration = 0.0;
    data.overall_bit_rate = 0;
    data.video.bitrate = 0;
    data.video.width = 0;
    data.video.height = 0;
    data.video.sampled_width = 0;
    data.video.sampled_height = 0;
    data.audio.bitrate = 0;
    data.audio.sampling_rate = 0;
    data.audio.bit_depth = 0;
    data.subtitle.format = "";
    
    // Parse each track
    for (rapidjson::SizeType i = 0; i < tracks.Size(); ++i) {
        const auto& track = tracks[i];
        if (!track.HasMember("@type") || !track["@type"].IsString()) {
            continue;
        }
        
        std::string track_type = track["@type"].GetString();
        
        if (track_type == "General") {
            data.originating_source_medium_id = extract_string_value(track, "OriginalSourceMedium_ID");
            data.originating_source_form = extract_string_value(track, "OriginalSourceForm");
            data.originating_network_name = extract_string_value(track, "OriginalNetworkName");
            data.format_commercial_if_any = extract_string_value(track, "Format_Commercial_IfAny");
            data.file_size = extract_int64_value(track, "FileSize");
            data.duration = extract_double_value(track, "Duration");
            data.overall_bit_rate = extract_int64_value(track, "OverallBitRate");
            data.domain = extract_string_value(track, "Domain");
            data.collection = extract_string_value(track, "Collection");
            data.season = extract_string_value(track, "Season");
            data.distributed_by = extract_string_value(track, "DistributedBy");
            data.genre = extract_string_value(track, "Genre");
            data.content_type = extract_string_value(track, "ContentType");
            data.owner = extract_string_value(track, "Owner");
            data.country = extract_string_value(track, "Country");
            data.comment = extract_string_value(track, "Comment");
            data.language = extract_string_value(track, "Language");
            data.video_creation_metadata = extract_string_value(track, "Encoded_Date");
            if (data.video_creation_metadata.empty()) {
                data.video_creation_metadata = extract_string_value(track, "Tagged_Date");
            }
        }
        else if (track_type == "Video") {
            data.video.codec = extract_string_value(track, "Format");
            data.video.codec_version = extract_string_value(track, "CodecID_Version");
            data.video.bitrate = extract_int64_value(track, "BitRate");
            data.video.width = static_cast<int>(extract_int64_value(track, "Width"));
            data.video.height = static_cast<int>(extract_int64_value(track, "Height"));
            data.video.sampled_width = static_cast<int>(extract_int64_value(track, "SampledWidth"));
            data.video.sampled_height = static_cast<int>(extract_int64_value(track, "SampledHeight"));
            data.video.frame_rate = extract_string_value(track, "FrameRate");
        }
        else if (track_type == "Audio") {
            if (data.audio.codec.empty()) {
                data.audio.codec = extract_string_value(track, "Format");
                data.audio.codec_version = extract_string_value(track, "CodecID_Version");
                data.audio.bitrate = extract_int64_value(track, "BitRate");
                data.audio.sampling_rate = static_cast<int>(extract_int64_value(track, "SamplingRate"));
                data.audio.channels = extract_string_value(track, "Channels");
                data.audio.bit_depth = static_cast<int>(extract_int64_value(track, "BitDepth"));
            }
            std::string lang = extract_string_value(track, "Language");
            if (!lang.empty() && std::find(data.audio.languages.begin(), data.audio.languages.end(), lang) == data.audio.languages.end()) {
                data.audio.languages.push_back(lang);
            }
        }
        else if (track_type == "Text") {
            std::string lang = extract_string_value(track, "Language");
            if (!lang.empty() && std::find(data.subtitle.languages.begin(), data.subtitle.languages.end(), lang) == data.subtitle.languages.end()) {
                data.subtitle.languages.push_back(lang);
            }
            if (data.subtitle.format.empty()) {
                data.subtitle.format = extract_string_value(track, "Format");
            }
        }
    }
    
    return true;
}

std::string MediaInfoExtractor::extract_string_value(const rapidjson::Value& obj, const char* key) {
    if (obj.HasMember(key) && obj[key].IsString()) {
        return obj[key].GetString();
    }
    return "";
}

std::int64_t MediaInfoExtractor::extract_int64_value(const rapidjson::Value& obj, const char* key) {
    if (obj.HasMember(key)) {
        if (obj[key].IsInt64()) {
            return obj[key].GetInt64();
        }
        if (obj[key].IsString()) {
            try {
                return std::stoll(obj[key].GetString());
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}

double MediaInfoExtractor::extract_double_value(const rapidjson::Value& obj, const char* key) {
    if (obj.HasMember(key)) {
        if (obj[key].IsDouble()) {
            return obj[key].GetDouble();
        }
        if (obj[key].IsString()) {
            try {
                return std::stod(obj[key].GetString());
            } catch (...) {
                return 0.0;
            }
        }
    }
    return 0.0;
}