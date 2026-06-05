# Run a simple test to see if libtorrent actually writes files
cat > /tmp/test_write.cpp << 'EOF'
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

namespace lt = libtorrent;

int main() {
    lt::settings_pack pack;
    pack.set_bool(lt::settings_pack::enable_dht, true);
    lt::session session(pack);
    
    std::string torrent_file = "./ubuntu-22.04.5-desktop-amd64.iso.torrent";
    auto ti = std::make_shared<lt::torrent_info>(torrent_file);
    
    lt::add_torrent_params params = {};
    params.ti = ti;
    params.save_path = "/tmp/test_dl";
    
    std::vector<lt::download_priority_t> priorities;
    priorities.resize(ti->num_files(), lt::download_priority_t{0});
    priorities[0] = lt::download_priority_t{1};
    params.file_priorities = priorities;
    
    lt::torrent_handle handle = session.add_torrent(params);
    
    // Wait for metadata
    while (!handle.status().has_metadata) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    handle.force_reannounce();
    
    // Wait for download
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto file_path = ti->files().file_path(lt::file_index_t(0));
        std::string full_path = "/tmp/test_dl/" + file_path;
        
        std::ifstream f(full_path, std::ios::binary);
        if (f.is_open()) {
            f.seekg(0, std::ios::end);
            auto size = f.tellg();
            f.close();
            std::cout << "File size: " << size << " bytes" << std::endl;
            if (size > 0) break;
        }
    }
    
    return 0;
}
EOF

g++ -std=c++17 /tmp/test_write.cpp -o /tmp/test_write -ltorrent-rasterbar -lboost_system -pthread
/tmp/test_write
