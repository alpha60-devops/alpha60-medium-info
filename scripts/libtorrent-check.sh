# Check installed version
rpm -qi rb_libtorrent

# Check what symbols are available
nm -D /usr/lib64/libtorrent-rasterbar.so | grep "torrent_info.*C1" | head -20