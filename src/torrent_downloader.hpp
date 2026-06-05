#ifndef TORRENT_DOWNLOADER_HPP
#define TORRENT_DOWNLOADER_HPP

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <filesystem>
#include <string>
#include <optional>
#include <atomic>

namespace fs = std::filesystem;
namespace lt = libtorrent;

class TorrentDownloader {
public:
    TorrentDownloader();
    ~TorrentDownloader();
    
    // Download only the first 'bytes_to_download' bytes of the media file
    // Returns path to the downloaded partial file, or empty if failed
    std::optional<fs::path> download_minimal(
        const std::string& torrent_path,
        const std::string& output_dir,
        std::int64_t bytes_to_download = 10 * 1024 * 1024  // 10 MB default
    );
    
private:
    lt::session session_;
    std::atomic<bool> session_running_{true};
    
    void drain_alerts();
};

#endif // TORRENT_DOWNLOADER_HPP