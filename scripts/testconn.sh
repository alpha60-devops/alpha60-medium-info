# Create a simple test program
cat > /tmp/test_connectivity.cpp << 'EOF'
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/alert_types.hpp>
#include <iostream>
#include <thread>
#include <chrono>

namespace lt = libtorrent;

int main() {
    lt::settings_pack pack;
    pack.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
    pack.set_bool(lt::settings_pack::enable_dht, true);
    pack.set_bool(lt::settings_pack::enable_lsd, true);
    pack.set_int(lt::settings_pack::connections_limit, 100);
    
    lt::session session(pack);
    
    lt::add_torrent_params params = {};
    params.ti = std::make_shared<lt::torrent_info>("/tmp/ubuntu-22.04.5-desktop-amd64.iso.torrent");
    params.save_path = "/tmp/test_dl";
    params.flags = lt::torrent_flags_t{};
    
    lt::torrent_handle handle = session.add_torrent(params);
    
    std::cout << "Starting download, waiting 30 seconds..." << std::endl;
    
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
EOF

# Compile and run
cd /tmp
g++ -std=c++17 test_connectivity.cpp -o test_connectivity -ltorrent-rasterbar -lboost_system -pthread
mkdir -p /tmp/test_dl
./test_connectivity