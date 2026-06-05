#include "json_enricher.hpp"

JsonEnricher::JsonEnricher() {}

std::string JsonEnricher::build_output(const std::vector<TorrentFile>& torrents,
                                        const std::vector<MediaInfoData>& media_data) {
    std::stringstream ss;
    
    // Get current UTC time
    auto now = std::time(nullptr);
    std::stringstream time_ss;
    time_ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ");
    
    ss << "{\n";
    ss << "  \"api_version\": \"1.0\",\n";
    ss << "  \"generated_at\": \"" << time_ss.str() << "\",\n";
    ss << "  \"total_objects\": " << torrents.size() << ",\n";
    ss << "  \"media_objects\": [\n";
    
    for (size_t i = 0; i < torrents.size(); ++i) {
        const auto& tf = torrents[i];
        const auto& md = (i < media_data.size()) ? media_data[i] : MediaInfoData();
        
        ss << "    {\n";
        ss << "      \"btih\": \"" << escape_json_string(tf.btih) << "\",\n";
        ss << "      \"name\": \"" << escape_json_string(tf.name) << "\",\n";
        ss << "      \"technical_metadata\": {\n";
        
        // Originating source fields
        ss << "        \"originating_source_medium_id\": " << json_string(md.originating_source_medium_id) << ",\n";
        ss << "        \"originating_source_form\": " << json_string(md.originating_source_form) << ",\n";
        ss << "        \"originating_network_name\": " << json_string(md.originating_network_name) << ",\n";
        
        // Container fields
        ss << "        \"format_commercial_if_any\": " << json_string(md.format_commercial_if_any) << ",\n";
        ss << "        \"file_size\": " << md.file_size << ",\n";
        ss << "        \"duration\": " << (md.duration > 0 ? std::to_string(md.duration) : "null") << ",\n";
        ss << "        \"overall_bit_rate\": " << (md.overall_bit_rate > 0 ? std::to_string(md.overall_bit_rate) : "null") << ",\n";
        ss << "        \"domain\": " << json_string(md.domain) << ",\n";
        ss << "        \"collection\": " << json_string(md.collection) << ",\n";
        ss << "        \"season\": " << json_string(md.season) << ",\n";
        ss << "        \"distributed_by\": " << json_string(md.distributed_by) << ",\n";
        ss << "        \"genre\": " << json_string(md.genre) << ",\n";
        ss << "        \"content_type\": " << json_string(md.content_type) << ",\n";
        ss << "        \"owner\": " << json_string(md.owner) << ",\n";
        ss << "        \"country\": " << json_string(md.country) << ",\n";
        ss << "        \"comment\": " << json_string(md.comment) << ",\n";
        ss << "        \"language\": " << json_string(md.language) << ",\n";
        
        // Video fields
        ss << "        \"video_codec\": " << json_string(md.video.codec) << ",\n";
        ss << "        \"video_codec_version\": " << json_string(md.video.codec_version) << ",\n";
        ss << "        \"video_bitrate\": " << (md.video.bitrate > 0 ? std::to_string(md.video.bitrate) : "null") << ",\n";
        ss << "        \"video_width\": " << (md.video.width > 0 ? std::to_string(md.video.width) : "null") << ",\n";
        ss << "        \"video_height\": " << (md.video.height > 0 ? std::to_string(md.video.height) : "null") << ",\n";
        ss << "        \"video_width_sampled\": " << (md.video.sampled_width > 0 ? std::to_string(md.video.sampled_width) : "null") << ",\n";
        ss << "        \"video_height_sampled\": " << (md.video.sampled_height > 0 ? std::to_string(md.video.sampled_height) : "null") << ",\n";
        ss << "        \"video_creation_metadata\": " << json_string(md.video_creation_metadata) << ",\n";
        ss << "        \"video_frame_rate\": " << json_string(md.video.frame_rate) << ",\n";
        
        // Audio fields
        ss << "        \"audio_codec\": " << json_string(md.audio.codec) << ",\n";
        ss << "        \"audio_codec_version\": " << json_string(md.audio.codec_version) << ",\n";
        ss << "        \"audio_bitrate\": " << (md.audio.bitrate > 0 ? std::to_string(md.audio.bitrate) : "null") << ",\n";
        ss << "        \"audio_sampling_rate\": " << (md.audio.sampling_rate > 0 ? std::to_string(md.audio.sampling_rate) : "null") << ",\n";
        ss << "        \"audio_channels\": " << json_string(md.audio.channels) << ",\n";
        ss << "        \"audio_bit_depth\": " << (md.audio.bit_depth > 0 ? std::to_string(md.audio.bit_depth) : "null") << ",\n";
        
        // Audio languages array
        ss << "        \"audio_languages\": [";
        for (size_t j = 0; j < md.audio.languages.size(); ++j) {
            if (j > 0) ss << ", ";
            ss << "\"" << escape_json_string(md.audio.languages[j]) << "\"";
        }
        ss << "],\n";
        
        // Subtitle fields
        ss << "        \"subtitle_languages\": [";
        for (size_t j = 0; j < md.subtitle.languages.size(); ++j) {
            if (j > 0) ss << ", ";
            ss << "\"" << escape_json_string(md.subtitle.languages[j]) << "\"";
        }
        ss << "],\n";
        ss << "        \"subtitle_format\": " << json_string(md.subtitle.format) << "\n";
        
        ss << "      }\n";
        ss << "    }";
        if (i < torrents.size() - 1) ss << ",";
        ss << "\n";
    }
    
    ss << "  ]\n";
    ss << "}\n";
    
    return ss.str();
}

std::string JsonEnricher::json_string(const std::string& str) {
    if (str.empty()) {
        return "null";
    }
    return "\"" + escape_json_string(str) + "\"";
}

bool JsonEnricher::write_output(const std::string& output_path, const std::string& json_content) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        return false;
    }
    file << json_content;
    return true;
}

std::string JsonEnricher::escape_json_string(const std::string& str) {
    std::stringstream ss;
    for (char c : str) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << c; break;
        }
    }
    return ss.str();
}
