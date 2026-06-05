#include "torrent_parser.hpp"

namespace lt = libtorrent;

TorrentParser::TorrentParser(const fs::path& idir)
    : input_directory_(idir) {}

std::vector<TorrentFile> TorrentParser::parse_all_torrents() {
    std::vector<TorrentFile> results;
    
    if (!fs::exists(input_directory_) || !fs::is_directory(input_directory_)) {
        throw std::runtime_error("Input directory does not exist: " + input_directory_.string());
    }
    
    for (const auto& entry : fs::directory_iterator(input_directory_)) {
        if (entry.path().extension() == ".torrent") {
            auto torrent = parse_single_torrent(entry.path());
            if (torrent.has_value()) {
                results.push_back(torrent.value());
            }
        }
    }
    
    return results;
}

std::optional<TorrentFile> TorrentParser::parse_single_torrent(const fs::path& torrent_file) {
    try {
        lt::error_code ec;
        // Use explicit string constructor to avoid ambiguity
        std::string path_str = torrent_file.string();
        lt::torrent_info ti(path_str, ec);
        
        if (ec) {
            return std::nullopt;
        }
        
        TorrentFile tf;
        tf.name = ti.name();
        tf.torrent_path = torrent_file;
        
        // Compute BTIH from file data (more reliable than ti.info_hash())
        auto torrent_data = read_torrent_file(torrent_file);
        tf.btih = compute_info_hash(torrent_data);
        
        // Get file information
        const auto& files = ti.files();
        tf.total_size = files.total_size();
        
        for (auto i : files.file_range()) {
            tf.file_paths.push_back(files.file_path(i));
            tf.file_sizes.push_back(files.file_size(i));
        }
        
        return tf;
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

std::string TorrentParser::compute_info_hash(const std::vector<char>& torrent_data) {
    lt::error_code ec;
    lt::bdecode_node node = lt::bdecode(
        lt::span<const char>(torrent_data.data(), torrent_data.size()), ec);
    if (ec) return "";
    
    lt::bdecode_node info = node.dict_find_dict("info");
    if (!info) return "";
    
    // Bencode the info dictionary
    std::vector<char> encoded;
    lt::bencode(std::back_inserter(encoded), info);
    
    // Calculate SHA-1 hash using OpenSSL
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(encoded.data()), encoded.size(), hash);
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        ss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return ss.str();
}

std::vector<char> TorrentParser::read_torrent_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        return {};
    }
    
    return buffer;
}
