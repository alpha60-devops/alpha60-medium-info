#include "torrent_downloader.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <iomanip>

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

  string ua(LIBTORRENT_VERSION);
  settings.set_str(settings_pack::user_agent, ua);

  settings.set_bool(settings_pack::enable_lsd, true);
  settings.set_bool(settings_pack::enable_dht, true);
  settings.set_bool(settings_pack::enable_upnp, true);
  settings.set_bool(settings_pack::enable_natpmp, true);

  settings.set_bool(settings_pack::announce_to_all_tiers, true);
  settings.set_bool(settings_pack::announce_to_all_trackers, true);

  settings.set_int(settings_pack::active_limit, 8);

  // REMOVE ARTIFICIAL SPEED LIMITS (0 = unlimited)
  settings.set_int(settings_pack::upload_rate_limit, 0);
  settings.set_int(settings_pack::download_rate_limit, 0);

  settings.set_int(settings_pack::auto_scrape_interval, 60);
  settings.set_int(settings_pack::auto_scrape_min_interval, 30);

  settings.set_int(settings_pack::max_pex_peers, 5000);

  settings.set_int(settings_pack::num_want, 1600);
  settings.set_int(settings_pack::connections_limit, 5000);

  settings.set_int(settings_pack::peer_timeout, 30);
  settings.set_int(settings_pack::inactivity_timeout, 30);

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
{
  session_running_ = false;
  session_.abort();
}

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

      // Find the largest file in the torrent
      const auto& files = ti->files();
      lt::file_index_t largest_file_index{0};
      std::int64_t max_size = -1;

      for (int i = 0; i < ti->num_files(); ++i) {
	  lt::file_index_t idx{i};
	  if (files.file_size(idx) > max_size) {
	      max_size = files.file_size(idx);
	      largest_file_index = idx;
	  }
      }

      // Get output file path
      auto target_file_path = files.file_path(largest_file_index);
      fs::path output_file_path = fs::path(output_dir) / target_file_path;
      fs::create_directories(output_file_path.parent_path());

      std::cout << "  Targeting largest file: " << target_file_path << std::endl;
      std::cout << "  File total size: " << max_size / (1024 * 1024) << " MB" << std::endl;
      std::cout << "  Download Target: " << bytes_to_download / (1024 * 1024) << " MB" << std::endl;

      // Setup download parameters
      lt::add_torrent_params params = {};
      params.ti = ti;
      params.save_path = output_dir;
      params.flags = lt::torrent_flags_t{};

      // Set normal priority (7) for the largest file, skip others (0)
      std::vector<lt::download_priority_t> priorities;
      priorities.resize(ti->num_files(), lt::download_priority_t{0});
      priorities[static_cast<int>(largest_file_index)] = lt::download_priority_t{7};
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

      // Enable sequential download using non-deprecated flag method
      handle.set_flags(lt::torrent_flags::sequential_download);

      // Force tracker announce
      handle.force_reannounce();

      auto target = static_cast<std::int64_t>(bytes_to_download);
      auto start_time = std::chrono::steady_clock::now();
      int zero_peer_count = 0;
      bool data_started = false;
      auto last_status_time = start_time;
      
      // For rate calculation
      std::int64_t last_downloaded = 0;
      auto last_rate_time = start_time;
      double current_rate_bps = 0.0;
      bool download_failed = false;
      std::string failure_reason;

      while (session_running_) {
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

	if (elapsed > timeout_seconds) {
	  failure_reason = "timeout";
          download_failed = true;
	  break;
	}

        auto status = handle.status();

        // Check if we have any payload download
        if (status.total_payload_download > 0) {
            data_started = true;
            zero_peer_count = 0;
        }

        // Check if we've reached the target
        if (status.total_payload_download >= target) {
	    std::cout << "  Downloaded " << status.total_payload_download / (1024*1024) << " MB" << std::endl;
	    break;
        }

        // Early abort if no peers and no data for too long
        if (status.num_peers == 0) {
            zero_peer_count++;
            if (zero_peer_count > 30 && !data_started) {
                failure_reason = "no peers found for 30 seconds";
                download_failed = true;
                break;
            }
        } else {
            zero_peer_count = 0;
        }

        // Calculate current download rate (bytes per second)
        auto rate_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_rate_time).count();
        if (rate_elapsed >= 5 && data_started) {
            std::int64_t delta_bytes = status.total_payload_download - last_downloaded;
            current_rate_bps = static_cast<double>(delta_bytes) / static_cast<double>(rate_elapsed);
            last_downloaded = status.total_payload_download;
            last_rate_time = now;
        }

        // Show progress every 5 seconds
        auto status_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_status_time).count();
        if (status_elapsed >= 5) {
            double downloaded_mb = status.total_payload_download / (1024.0 * 1024.0);
            double target_mb = target / (1024.0 * 1024.0);
            double rate_kbps = current_rate_bps / 1024.0;
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "  Peers: " << status.num_peers 
                      << " | Downloaded: " << downloaded_mb << " MB / " << target_mb << " MB"
                      << " | Speed: " << rate_kbps << " KB/s";
            if (current_rate_bps > 0 && data_started) {
                double eta_seconds = (target - status.total_payload_download) / current_rate_bps;
                if (eta_seconds > 0 && eta_seconds < 3600) {
                    std::cout << " | ETA: " << static_cast<int>(eta_seconds) << "s";
                }
            }
            std::cout << std::endl;
            last_status_time = now;
        }

	drain_alerts();
	std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      // Clean up
      handle.pause();
      session_.remove_torrent(handle);

      // If download failed but we have some data, estimate time to complete TARGET (not full file)
      if (download_failed && data_started && current_rate_bps > 0)
	{
	  auto status = handle.status(); // get fresh status
	  std::int64_t downloaded_bytes = status.total_payload_download;
	  double downloaded_mb = downloaded_bytes / (1024.0 * 1024.0);
	  double target_mb = target / (1024.0 * 1024.0);
	  double rate_kbps = current_rate_bps / 1024.0;
	  double remaining_bytes = target - downloaded_bytes;
	  if (remaining_bytes > 0) {
	      double eta_seconds = remaining_bytes / current_rate_bps;
	      std::cerr << std::fixed << std::setprecision(2);
	      std::cerr << "  Download failed (" << failure_reason << ")" << std::endl;
	      std::cerr << "  Downloaded " << downloaded_mb << " MB of " << target_mb 
			<< " MB target" << std::endl;
	      std::cerr << "  At current rate (" << rate_kbps << " KB/s), completing the TARGET ("
			<< target_mb << " MB) would take ~" << static_cast<int>(eta_seconds / 60) 
			<< " minutes " << static_cast<int>(eta_seconds) % 60 << " seconds" << std::endl;
	  } else {
	      std::cerr << "  Download failed (" << failure_reason << ") but target was reached" << std::endl;
	  }
	}
      else if (download_failed)
	{
	  std::cerr << "  Download failed (" << failure_reason << "), no data transferred" << std::endl;
	}

      // Verify we have real data on disk
      if (!download_failed && fs::exists(output_file_path) && fs::file_size(output_file_path) > 0) {
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