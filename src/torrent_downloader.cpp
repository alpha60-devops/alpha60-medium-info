#include "torrent_downloader.hpp"

namespace lt = libtorrent;

/// Copied from alpha60-torrent.h
/// Configure torrent session.
lt::settings_pack
make_settings_pack()
{
  using string = std::string;
  using settings_pack = lt::settings_pack;

  settings_pack settings;

  // Bittorrent port range is 6881-6889. KG requires 49152-65535.
  settings.set_str(settings_pack::listen_interfaces, "0.0.0.0:65505");

  //string ua("alpha60.co/" LIBTORRENT_VERSION);
  string ua(LIBTORRENT_VERSION);
  settings.set_str(settings_pack::user_agent, ua);

  settings.set_bool(settings_pack::enable_lsd, true);
  settings.set_bool(settings_pack::enable_dht, true);
  settings.set_bool(settings_pack::enable_upnp, true); // 2026.1
  settings.set_bool(settings_pack::enable_natpmp, true); // 2026.2

  settings.set_bool(settings_pack::announce_to_all_tiers, true);
  settings.set_bool(settings_pack::announce_to_all_trackers, true);

  settings.set_int(settings_pack::active_limit, 1);

  settings.set_int(settings_pack::upload_rate_limit, 120);
  settings.set_int(settings_pack::download_rate_limit, 120);
  settings.set_int(settings_pack::auto_scrape_interval, 60); // 1800
  settings.set_int(settings_pack::auto_scrape_min_interval, 30); // 300

  settings.set_int(settings_pack::max_pex_peers, 5000); // 50

  // Global limit, default is 200. Per torrent limit via torrent_handle.
  settings.set_int(settings_pack::num_want, 1600);
  settings.set_int(settings_pack::connections_limit, 5000);

  // Set timeouts.
  settings.set_int(settings_pack::peer_timeout, 30); // 120
  settings.set_int(settings_pack::inactivity_timeout, 30); // 600

  return settings;
}


media_downloader::media_downloader()
{
  lt::settings_pack pack = make_settings_pack();

  using namespace lt::alert_category;
  auto pack_cat(error | storage | status | tracker | dht);
  pack.set_int(lt::settings_pack::alert_mask, pack_cat);
  session_.apply_settings(pack);
}

media_downloader::~media_downloader()
{ session_.abort(); }

void
media_downloader::drain_alerts()
{
  std::vector<lt::alert*> alerts;
  session_.pop_alerts(&alerts);
  for (lt::alert* alert : alerts)
    {
      if (auto* te = lt::alert_cast<lt::torrent_error_alert>(alert))
	std::cerr << "  [ERROR] " << te->error.message() << std::endl;
    }
}

std::optional<fs::path>
media_downloader::download_minimal(const std::string& torrent_path,
				   const std::string& output_dir,
				   const std::int64_t bytes_to_download,
				   const int timeout_seconds)
{
  try
    {
      fs::create_directories(output_dir);

      // Create torrent_info
      std::string path_str = torrent_path;
      auto ti = std::make_shared<lt::torrent_info>(path_str);

      if (ti->num_files() == 0)
	return std::nullopt;

      // Get output file path
      const auto& files = ti->files();
      auto first_file_path = files.file_path(lt::file_index_t(0));
      fs::path output_file_path = fs::path(output_dir) / first_file_path;
      fs::create_directories(output_file_path.parent_path());

      std::cout << "  File: " << first_file_path << std::endl;
      std::cout << "  Target: " << bytes_to_download / (1024*1024) << " MB" << std::endl;

      // Setup download parameters
      lt::add_torrent_params params = {};
      params.ti = ti;
      params.save_path = output_dir;
      params.flags = lt::torrent_flags_t{};

      // Only download the first file
      std::vector<lt::download_priority_t> priorities;
      priorities.resize(ti->num_files(), lt::download_priority_t{0});
      priorities[0] = lt::download_priority_t{1};
      params.file_priorities = priorities;

      // Add torrent
      lt::torrent_handle handle = session_.add_torrent(std::move(params));

      // Wait for metadata
      std::cout << "  Waiting for metadata..." << std::endl;
      for (int attempt = 0; attempt < 60; ++attempt) {
	if (handle.status().has_metadata) break;
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	drain_alerts();
      }

      if (!handle.status().has_metadata) {
	session_.remove_torrent(handle);
	return std::nullopt;
      }

      std::cout << "  Metadata received, enabling sequential download..." << std::endl;

      // Enable sequential download to get the beginning of the file first
      handle.set_sequential_download(true);

      // Force tracker announce
      handle.force_reannounce();

      // Monitor actual file size on disk
      auto target = static_cast<std::uint64_t>(bytes_to_download);
      auto start_time = std::chrono::steady_clock::now();

      while (true) {
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

	if (elapsed > timeout_seconds) {
	  std::cerr << "  Timeout after " << timeout_seconds << " seconds" << std::endl;
	  break;
	}

	// Check actual file on disk
	if (fs::exists(output_file_path)) {
	  auto file_size = fs::file_size(output_file_path);

	  if (file_size >= target && file_size > 0) {
	    // Verify we have real data (not zeros)
	    std::ifstream verify(output_file_path, std::ios::binary);
	    char first_byte;
	    verify.read(&first_byte, 1);
	    verify.close();

	    if (first_byte != 0) {
	      std::cout << "  Downloaded " << file_size / (1024*1024) << " MB" << std::endl;
	      break;
	    }
	  }

	  // Show progress
	  static auto last_progress = start_time;
	  if (elapsed - std::chrono::duration_cast<std::chrono::seconds>(last_progress - start_time).count() >= 5) {
	    if (file_size > 0) {
	      std::cout << "  Progress: " << file_size / (1024*1024)
			<< " MB / " << target / (1024*1024) << " MB" << std::endl;
	    } else if (elapsed % 10 == 0) {
	      auto status = handle.status();
	      std::cout << "  Peers: " << status.num_peers << std::endl;
	    }
	    last_progress = now;
	  }
	} else {
	  // File doesn't exist yet
	  if (elapsed % 10 == 0 && elapsed > 0) {
	    auto status = handle.status();
	    std::cout << "  Waiting for file creation, peers: " << status.num_peers << std::endl;
	  }
	}

	drain_alerts();
	std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      // Clean up
      handle.pause();
      session_.remove_torrent(handle);

      // Verify we have real data
      if (fs::exists(output_file_path) && fs::file_size(output_file_path) >= target) {
	std::ifstream verify(output_file_path, std::ios::binary);
	char first_byte;
	verify.read(&first_byte, 1);
	verify.close();

	if (first_byte != 0) {
	  return output_file_path;
	} else {
	  std::cerr << "  Downloaded file exists but contains zeros - no peers found" << std::endl;
	}
      }

      return std::nullopt;

    } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return std::nullopt;
  }
}
