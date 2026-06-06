#include "torrent_downloader.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <iomanip>

namespace lt = libtorrent;

/// Configure torrent session.
lt::settings_pack
make_settings_pack()
{
  using string = std::string;
  using settings_pack = lt::settings_pack;

  settings_pack settings;

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

  settings.set_int(settings_pack::upload_rate_limit, 0);
  settings.set_int(settings_pack::download_rate_limit, 0);

  settings.set_int(settings_pack::auto_scrape_interval, 60);
  settings.set_int(settings_pack::auto_scrape_min_interval, 30);

  settings.set_int(settings_pack::max_pex_peers, 5000);

  settings.set_int(settings_pack::num_want, 1600);
  settings.set_int(settings_pack::connections_limit, 5000);

  settings.set_int(settings_pack::peer_timeout, 30);
  settings.set_int(settings_pack::inactivity_timeout, 30);

  // Disk I/O settings for libtorrent 2.0
  settings.set_int(settings_pack::disk_io_read_mode, 0);
  settings.set_int(settings_pack::disk_io_write_mode, 0);
  settings.set_bool(settings_pack::no_atime_storage, true);

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

/// Copy the first N bytes from source file to destination file
static bool
copy_first_n_bytes(const fs::path& source, const fs::path& destination, std::int64_t num_bytes)
{
  std::ifstream src(source, std::ios::binary);
  if (!src.is_open()) {
    std::cerr << "  [ERROR] Cannot open source file: " << source << std::endl;
    return false;
  }

  std::ofstream dst(destination, std::ios::binary);
  if (!dst.is_open()) {
    std::cerr << "  [ERROR] Cannot create destination file: " << destination << std::endl;
    return false;
  }

  const size_t buffer_size = 1024 * 1024; // 1MB buffer
  std::vector<char> buffer(buffer_size);
  std::int64_t remaining = num_bytes;
  std::int64_t total_copied = 0;

  while (remaining > 0) {
    size_t to_read = static_cast<size_t>(std::min(remaining, static_cast<std::int64_t>(buffer_size)));
    src.read(buffer.data(), to_read);
    std::streamsize bytes_read = src.gcount();
    if (bytes_read == 0) break;
    dst.write(buffer.data(), bytes_read);
    remaining -= bytes_read;
    total_copied += bytes_read;
  }

  dst.flush();
  dst.close();
  src.close();

  std::cout << "  Copied " << total_copied / (1024*1024) << " MB to " << destination.filename() << std::endl;
  return total_copied >= num_bytes;
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

      std::string path_str = torrent_path;
      auto ti = std::make_shared<lt::torrent_info>(path_str);

      if (ti->num_files() == 0)
	return std::nullopt;

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

      auto target_file_path = files.file_path(largest_file_index);
      fs::path final_file_path = fs::path(output_dir) / target_file_path;
      fs::path sized_file_path = final_file_path.string() + ".sized";
      fs::create_directories(final_file_path.parent_path());

      std::cout << "  Targeting largest file: " << target_file_path << std::endl;
      std::cout << "  File total size: " << max_size / (1024 * 1024) << " MB" << std::endl;
      std::cout << "  Download Target: " << bytes_to_download / (1024 * 1024) << " MB" << std::endl;

      lt::add_torrent_params params = {};
      params.ti = ti;
      params.save_path = output_dir;
      params.flags = lt::torrent_flags_t{};
      params.storage_mode = lt::storage_mode_allocate;

      std::vector<lt::download_priority_t> priorities;
      priorities.resize(ti->num_files(), lt::download_priority_t{0});
      priorities[static_cast<int>(largest_file_index)] = lt::download_priority_t{7};
      params.file_priorities = priorities;

      lt::torrent_handle handle = session_.add_torrent(std::move(params));

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
      handle.set_flags(lt::torrent_flags::sequential_download);
      handle.set_flags(lt::torrent_flags::auto_managed);
      handle.force_reannounce();

      auto target = static_cast<std::int64_t>(bytes_to_download);
      auto start_time = std::chrono::steady_clock::now();
      auto last_status_time = start_time;

      std::int64_t last_downloaded = 0;
      auto last_rate_time = start_time;
      double current_rate_bps = 0.0;
      bool data_confirmed = false;
      bool sized_file_created = false;

      while (session_running_) {
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

	if (elapsed > timeout_seconds) {
	  std::cerr << "  Timeout after " << timeout_seconds << " seconds" << std::endl;
	  break;
	}

	auto status = handle.status();

	// Check if we've reached the target
	if (status.total_payload_download >= target && data_confirmed) {
	    std::cout << "  Downloaded " << status.total_payload_download / (1024*1024) << " MB" << std::endl;
	    break;
	}

	// Check actual file on disk for non-zero data
	if (!data_confirmed && fs::exists(final_file_path) && fs::file_size(final_file_path) > 0) {
	    std::ifstream verify(final_file_path, std::ios::binary);
	    char first_byte;
	    verify.read(&first_byte, 1);
	    verify.close();
	    if (first_byte != 0) {
		data_confirmed = true;
		std::cout << "  Data confirmed on disk at offset 0" << std::endl;
	    }
	}

	// Create .sized file once we have enough data
	if (data_confirmed && !sized_file_created && fs::exists(final_file_path)) {
	    auto current_size = fs::file_size(final_file_path);
	    if (current_size >= static_cast<std::uint64_t>(target)) {
		std::cout << "  Creating .sized file with first " << target / (1024*1024) << " MB..." << std::endl;
		if (copy_first_n_bytes(final_file_path, sized_file_path, target)) {
		    sized_file_created = true;
		    std::cout << "  ✓ Created: " << sized_file_path.filename() << std::endl;
		} else {
		    std::cerr << "  ✗ Failed to create .sized file" << std::endl;
		}
	    }
	}

	// Calculate current download rate
	auto rate_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_rate_time).count();
	if (rate_elapsed >= 5 && status.total_payload_download > 0) {
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
	    if (data_confirmed && fs::exists(final_file_path)) {
		std::cout << " | Disk: " << fs::file_size(final_file_path) / (1024*1024) << " MB";
	    }
	    if (sized_file_created) {
		std::cout << " | .sized: ✓";
	    }
	    std::cout << std::endl;
	    last_status_time = now;
	}

	// Force a flush periodically
	if (elapsed % 5 == 0 && elapsed > 0) {
	    handle.force_reannounce();
	}

	drain_alerts();
	std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      // Clean up
      handle.pause();
      session_.remove_torrent(handle);

      // If we haven't created the .sized file yet, try one more time
      if (data_confirmed && !sized_file_created && fs::exists(final_file_path)) {
	  auto current_size = fs::file_size(final_file_path);
	  if (current_size >= static_cast<std::uint64_t>(target)) {
	      std::cout << "  Creating .sized file with first " << target / (1024*1024) << " MB (final attempt)..." << std::endl;
	      if (copy_first_n_bytes(final_file_path, sized_file_path, target)) {
		  sized_file_created = true;
		  std::cout << "  ✓ Created: " << sized_file_path.filename() << std::endl;
	      }
	  }
      }

      // Final verification: read first byte
      if (fs::exists(final_file_path) && fs::file_size(final_file_path) > 0) {
	  std::ifstream verify(final_file_path, std::ios::binary);
	  char first_byte;
	  verify.read(&first_byte, 1);
	  verify.close();

	  if (first_byte != 0) {
	      std::cout << "  Successfully downloaded " << fs::file_size(final_file_path) / (1024*1024) << " MB" << std::endl;
	      return final_file_path;
	  } else {
	      std::cerr << "  File exists but first byte is zero - no data written to disk" << std::endl;
	  }
      }

      return std::nullopt;

    } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return std::nullopt;
  }
}