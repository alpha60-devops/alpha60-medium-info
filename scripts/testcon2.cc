#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/alert_types.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

namespace lt = libtorrent;

int main() {
    lt::settings_pack pack;
    pack.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
    pack.set_bool(lt::settings_pack::enable_dht, true);
    pack.set_bool(lt::settings_pack::enable_lsd, true);
    pack.set_int(lt::settings_pack::connections_limit, 100);
    
    lt::session session(pack);
    
    // Use explicit string to avoid ambiguous constructor
    std::string torrent_file = "./ubuntu-22.04.5-desktop-amd64.iso.torrent";
    auto ti = std::make_shared<lt::torrent_info>(torrent_file);
    
    lt::add_torrent_params params = {};
    params.ti = ti;
    params.save_path = "./test_dl";
    params.flags = lt::torrent_flags_t{};
    
    lt::torrent_handle handle = session.add_torrent(params);
    
    std::cout << "Starting download, waiting 30 seconds..." << std::endl;
    std::cout << "Torrent: " << ti->name() << std::endl;
    
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto status = handle.status();
        std::cout << "Peers: " << status.num_peers 
                  << ", Downloaded: " << status.total_download / 1024 << " KB" << std::endl;
        
        // Check for alerts
        std::vector<lt::alert*> alerts;
        session.pop_alerts(&alerts);
        for (auto* alert : alerts) {
            if (auto* ta = lt::alert_cast<lt::tracker_reply_alert>(alert)) {
                std::cout << "  Tracker replied with " << ta->num_peers << " peers" << std::endl;
            }
        }
    }
    
    return 0;
}
