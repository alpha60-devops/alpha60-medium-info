#include "torrent_downloader.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>

namespace lt = libtorrent;

TorrentDownloader::TorrentDownloader() {
    lt::settings_pack pack;
    
    pack.set_bool(lt::settings_pack::enable_dht, true);
    pack.set_bool(lt::settings_pack::enable_lsd, true);
    pack.set_bool(lt::settings_pack::enable_upnp, true);
    pack.set_bool(lt::settings_pack::enable_natpmp, true);
    
    pack.set_int(lt::settings_pack::connections_limit, 200);
    pack.set_int(lt::settings_pack::active_downloads, 5);
    pack.set_int(lt::settings_pack::download_rate_limit, 0);
    pack.set_int(lt::settings_pack::upload_rate_limit, 0);
    
    pack.set_int(lt::settings_pack::alert_mask, 
        lt::alert_category::error | 
        lt::alert_category::storage |
        lt::alert_category::status |
        lt::alert_category::tracker |
        lt::alert_category::dht);
    
    session_.apply_settings(pack);
}

TorrentDownloader::~TorrentDownloader() {
    session_.abort();
}

void TorrentDownloader::drain_alerts() {
    std::vector<lt::alert*> alerts;
    session_.pop_alerts(&alerts);
    for (lt::alert* alert : alerts) {
        if (auto* te = lt::alert_cast<lt::torrent_error_alert>(alert)) {
            std::cerr << "  [ERROR] " << te->error.message() << std::endl;
        }
    }
}

std::optional<fs::path> TorrentDownloader::download_minimal(
    const std::string& torrent_path,
    const std::string& output_dir,
    std::int64_t bytes_to_download) {
    
    try {
        fs::create_directories(output_dir);
        
        // Create torrent_info
        std::string path_str = torrent_path;
        auto ti = std::make_shared<lt::torrent_info>(path_str);
        
        if (ti->num_files() == 0) {
            return std::nullopt;
        }
        
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
        int timeout_seconds = 300;  // 5 minutes
        
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