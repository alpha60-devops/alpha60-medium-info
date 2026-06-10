#ifndef TORRENT_DOWNLOADER_HPP
#define TORRENT_DOWNLOADER_HPP

#include <atomic>
#include <string>
#include <optional>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>

namespace fs = std::filesystem;
namespace lt = libtorrent;

/// Downloader encapsulation.
class media_downloader
{
public:
  media_downloader();
  ~media_downloader();

  // Download only the first 'bytes_to_download' bytes of the media file
  // Returns path to the downloaded partial file, or empty if failed
  // 10 MB default,
  std::optional<fs::path>
  download_minimal(const std::string& torrent_path,
		   const std::string& output_dir,
		   const std::int64_t bytes_to_download = 10 * 1024 * 1024,
		   const int timeout_seconds = 300,
		   const std::string fsuffix = ".sized");

private:
  lt::session		session_;
  std::atomic<bool>	session_running_{true};

  void drain_alerts();
};

#endif // TORRENT_DOWNLOADER_HPP
