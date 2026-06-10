#include "torrent_downloader.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

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
#if 0
  settings.set_int(settings_pack::disk_io_read_mode, 0);
  settings.set_int(settings_pack::disk_io_write_mode, 0);
  settings.set_bool(settings_pack::no_atime_storage, true);
#endif

  // For downloading and trying to clip for small files, enable this. Don't do it in general.
  // Force immediate, synchronous writes to disk
  // This requires libtorrent version >= 2.0.6
  settings.set_int(settings_pack::disk_io_write_mode, settings_pack::write_through);

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
copy_first_n_bytes(const fs::path& source, const fs::path& destination,
		   const std::int64_t num_bytes)
{
  std::ifstream src(source, std::ios::binary);
  if (!src.is_open())
    {
      std::cerr << "[ERROR] Cannot open source file: " << source << std::endl;
      return false;
    }

  std::ofstream dst(destination, std::ios::binary);
  if (!dst.is_open())
    {
      std::cerr << "[ERROR] Cannot create destination file: " << destination << std::endl;
      return false;
  }

  std::int64_t buffer_size = 1024 * 1024; // 1MB buffer
  std::vector<char> buffer(buffer_size);
  std::int64_t remaining = num_bytes;
  std::int64_t total_copied = 0;

  while (remaining > 0)
    {
      size_t to_read = static_cast<size_t>(std::min(remaining, buffer_size));
      src.read(buffer.data(), to_read);
      std::streamsize bytes_read = src.gcount();
      if (bytes_read == 0)
	break;
      dst.write(buffer.data(), bytes_read);
      remaining -= bytes_read;
      total_copied += bytes_read;
    }

  dst.flush();
  dst.close();
  src.close();

  // Ensure data is written to disk before returning
  int fd = open(destination.string().c_str(), O_WRONLY);
  if (fd != -1)
    {
      if (fsync(fd) == 0)
	std::cout << "  fsync() confirmed for .sized file" << std::endl;
      else
	std::cerr << "  fsync() failed for .sized file: " << strerror(errno) << std::endl;
      close(fd);
    }

  return total_copied >= num_bytes;
}


/// @param file_path   path to the file to verify
/// @param min_size    minimum required file size (bytes)
/// @param bytes_to_check number of bytes to read from start (default 1024)
/// @return true if file exists, size >= min_size, and first bytes_to_check are non-zero
static bool
verify_data_on_disk(const fs::path& file_path,
		    const std::uint64_t min_size,
		    const std::size_t bytes_to_check = 1024)
{
  bool result = false;
  if (fs::exists(file_path))
    {
      const auto file_size = fs::file_size(file_path);
      if (file_size >= min_size)
	{
	  std::ifstream file(file_path, std::ios::binary);
	  if (file.is_open())
	    {
	      std::vector<char> buffer(bytes_to_check);
	      file.read(buffer.data(), bytes_to_check);
	      std::streamsize bytes_read = file.gcount();
	      file.close();

	      if (bytes_read > 0)
		{
		  // Check if at least one byte is non‑zero
		  bool found_nonzero = false;
		  for (std::streamsize i = 0; i < bytes_read; ++i)
		    {
		      if (buffer[i] != 0)
			{
			  found_nonzero = true;
			  break;
			}
		    }
		  if (found_nonzero)
		    result = true;
		}
	    }
	}
    }
  return result;
}


// Log if no peers to file.
const std::ios_base::openmode ofm = std::ios_base::out | std::ios_base::app;
std::ofstream ofno("download.suspect-or-no-peers.log", ofm);


/// Download a minimum-sized chunk of the largest media file so that
/// mediainfo can be used to determine the frame rate, frame size,
/// audio and subtitles.
///
/// Download the largest file in the @parm torrent_path given as an
/// argument, but stop at 10MB (or @param bytes_to_download) and
/// archive the smaller sized file.
///
/// @param timeout_seconds the number of seconds to loop while wating for data.
/// @param output_dir result files
/// @param fsuffix the suffix used on the minimal media file
std::optional<fs::path>
media_downloader::download_minimal(const std::string& torrent_path,
				   const std::string& output_dir,
				   const std::int64_t bytes_to_download,
				   const int timeout_seconds,
				   const std::string fsuffix)
{
  // Return sized_file_path file whenever possible, as that is the small one.
  using namespace std;

  fs::path final_file_path;
  fs::path sized_file_path;

  // Amount of time before downloading is considered futile.
  // This could be for a number of factors: no peers, private, unreachable.
  const uint unresponsive_seconds(30);

  try
    {
      fs::create_directories(output_dir);

      string path_str = torrent_path;
      auto ti = make_shared<lt::torrent_info>(path_str);

      if (ti->num_files() == 0)
	return nullopt;

      // Find the largest file, and prepare to download that.
      const auto& files = ti->files();
      lt::file_index_t largest_file_index{0};
      int64_t max_size = -1;
      for (int i = 0; i < ti->num_files(); ++i)
	{
	  lt::file_index_t idx{i};
	  if (files.file_size(idx) > max_size)
	    {
	      max_size = files.file_size(idx);
	      largest_file_index = idx;
	    }
	}
      const double max_mb = double(max_size) / (1024.0 * 1024.0);
      const double target = bytes_to_download;
      const double target_mb = double(target) / (1024.0 * 1024.0);

      auto target_file_path = files.file_path(largest_file_index);
      final_file_path = fs::path(output_dir) / target_file_path;
      sized_file_path = final_file_path.string() + fsuffix;
      fs::create_directories(final_file_path.parent_path());

      cout << "  target file: (" << target_mb << "/" << max_mb << ")"
		<< "\t" << target_file_path << endl;

      // Set up parameters.
      lt::add_torrent_params params = {};
      params.ti = ti;
      params.save_path = output_dir;
      params.flags = lt::torrent_flags_t{};
      //params.storage_mode = lt::storage_mode_allocate;
      params.storage_mode = lt::storage_mode_sparse;
      params.flags |= lt::torrent_flags::auto_managed;

      // Set up download priorities.
      vector<lt::download_priority_t> priorities;
      priorities.resize(ti->num_files(), lt::download_priority_t{0});
      priorities[static_cast<int>(largest_file_index)] = lt::download_priority_t{7};
      params.file_priorities = priorities;

      // Start BTIH in session...
      lt::torrent_handle handle = session_.add_torrent(move(params));
      cout << "starting, waiting for metadata...";
      for (int attempt = 0; attempt < 60; ++attempt)
	{
	  if (handle.status().has_metadata)
	    break;
	  this_thread::sleep_for(chrono::milliseconds(500));
	  drain_alerts();
	}
      if (!handle.status().has_metadata)
	{
	  session_.remove_torrent(handle);
	  return nullopt;
	}
      cout << "  ...metadata received." << endl;

      // Start loop...
      // End when:
      /// 1: enough to make sized_file
      /// 2: timeout
      auto start_time = chrono::steady_clock::now();
      auto last_status_time = start_time;

      auto to_seconds = [](auto duration)
      { return std::chrono::duration_cast<std::chrono::seconds>(duration); };

      int64_t last_downloaded = 0;
      auto last_rate_time = start_time;
      double current_rate_bps = 0.0;
      while (session_running_)
	{
	  // Start clock.
	  auto now = chrono::steady_clock::now();

	  // Check for timeout.
	  auto elapsed = to_seconds(now - start_time).count();
	  if (elapsed > timeout_seconds)
	    {
	      cerr << "timeout after " << timeout_seconds << " seconds" << endl;
	      break;
	    }

	  // Get status.
	  auto status = handle.status();

	  // Check if the download has reached the target size.
	  // status.total_payload_download
	  // status.all_time_download
	  const double downloaded_mb = status.total_done / (1024.0 * 1024.0);
	  const double xtra_mb = 5; // Stop slightly after total.
	  if ((downloaded_mb >= target_mb + xtra_mb || downloaded_mb >= max_mb))
	    break;

	  // Check if stalled.
	  if (downloaded_mb == 0 && elapsed > unresponsive_seconds)
	    break;

	  // Calculate current download rate
	  double rate_elapsed = to_seconds(now - last_rate_time).count();
	  if (rate_elapsed >= 5 && status.total_payload_download > 0)
	    {
	      double delta_bytes = status.total_payload_download - last_downloaded;
	      current_rate_bps = delta_bytes / rate_elapsed;
	      last_downloaded = status.total_payload_download;
	      last_rate_time = now;
	    }

	  // Show progress every n second interval.
	  const auto status_interval = 5;
	  auto status_elapsed = chrono::duration_cast<chrono::seconds>(now - last_status_time).count();
	  if (status_elapsed >= status_interval)
	    {
	      double rate_kbps = current_rate_bps / 1024.0;
	      cout << fixed << setprecision(2);
	      cout << "  Peers: " << status.num_peers
		   << " | Downloaded: " << downloaded_mb << " MB / "
		   << max_mb << " MB"
		   << " | Speed: " << rate_kbps << " KB/s";

	      cout << endl;
	      last_status_time = now;
	    }

	  // Force a flush periodically
	  if (elapsed % 5 == 0 && elapsed > 0)
	    handle.force_reannounce();

	  drain_alerts();
	  this_thread::sleep_for(chrono::seconds(1));
	}

      // Tear down.
      // Pause session, flush data, remove torrent.
      const double downloaded = handle.status().total_done;
      handle.pause();

      // Force cache flush
      handle.flush_cache();

      // Request resume data (this also forces dirty blocks to disk)
      handle.save_resume_data(lt::torrent_handle::save_info_dict);

      // If bytes were downloaded, wait for write to disk. If not, skip.
      if (downloaded)
	{
	  // Wait for flush to complete - monitor file or wait fixed time
	  // The safe approach: wait a few seconds for async writes to complete
	  // A better approach: poll file size/verify_data_on_disk with timeout
	  const int waitmaxsec = 8;
	  bool data_written = false;
	  for (int retry = 0; retry < waitmaxsec; ++retry)
	    {
	      if (verify_data_on_disk(final_file_path, target, 512))
		{
		  data_written = true;
		  break;
		}
	      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	    }
	  if (!data_written)
	    {
	      // OS-level flush
	      // Get the actual file path that libtorrent is writing to
	      // Open the file with POSIX for direct fsync
	      int fd = open(final_file_path.string().c_str(), O_RDONLY);
	      if (fd != -1)
		{
		  cout << "  Calling fsync() on file descriptor..." << endl;
		  if (fsync(fd) != 0)
		    cerr << "  ..fsync() failed: " << strerror(errno) << endl;
		  close(fd);
		}
	      else
		cerr << "  Invalid file for fsync: " << strerror(errno) << endl;
	    }
	}

      session_.remove_torrent(handle);
      this_thread::sleep_for(chrono::seconds(5));


      // Confirm data on disk.
      // Check actual file on disk for non-zero data.
      // Create a specially-sized small file for the media archive.
      const bool ff_created = fs::exists(final_file_path);
      const auto ff_size = ff_created ? fs::file_size(final_file_path) : 0;
      const bool ff_size_targetp = ff_size >= ulong(target);
      if (ff_created && ff_size_targetp)
	{
	  if (verify_data_on_disk(final_file_path, target))
	    {
	      if (!copy_first_n_bytes(final_file_path, sized_file_path, target))
		cerr << "fail: BTIH did not create smaller sized file" << endl;
	    }
	  else
	    cerr << "fail: BTIH did not serialize to disk" << endl;
	}


      // Clean up
      // Remove large file and used sized file if possible.
      const bool cleanupp(true);
      if (cleanupp && ff_created)
	{
	  error_code ec;
	  if (!fs::remove(final_file_path, ec))
	    cout << "error: failed to remove file: " << ec.message() << endl;
	}

      if (verify_data_on_disk(sized_file_path, target))
	return sized_file_path;
      else
	{
	  if (downloaded == 0)
	    {
	      ofno << torrent_path << endl;
	      ofno.flush();
	    }
	  return nullopt;
	}
    }

  catch (const exception& e)
    {
      cerr << "Exception: " << e.what() << endl;
      return nullopt;
    }
}
