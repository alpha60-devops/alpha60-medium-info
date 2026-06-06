# Check what symbols libtorrent actually exports
nm -D /home/bkoz/bin/H-libtorrent/lib64/libtorrent-rasterbar.so | grep torrent_info | head -20

# Check libtorrent's dependencies
ldd /home/bkoz/bin/H-libtorrent/lib64/libtorrent-rasterbar.so

# Check compiler ABI version
g++ --version

# Check if libtorrent was built with the same C++ ABI
objdump -p /home/bkoz/bin/H-libtorrent/lib64/libtorrent-rasterbar.so | grep SONAME

# Try compiling a minimal test
cat > test.cpp << 'EOF'
#include <libtorrent/torrent_info.hpp>
#include <vector>
int main() {
    std::vector<char> data;
    lt::torrent_info ti(lt::span<const char>(data.data(), data.size()), lt::from_span_t());
    return 0;
}
EOF

g++ -std=c++20 -I/home/bkoz/bin/H-libtorrent/include test.cpp -L/home/bkoz/bin/H-libtorrent/lib64 -ltorrent-rasterbar -lssl -lcrypto -lboost_system -lboost_filesystem -lboost_thread -lpthread -o test