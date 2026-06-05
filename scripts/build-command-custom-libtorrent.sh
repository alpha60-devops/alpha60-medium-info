cd media_enrichment
rm -rf build
mkdir build && cd build
cmake .. -DLIBTORRENT_INCLUDE_DIR=/home/bkoz/bin/H-libtorrent/include \
         -DLIBTORRENT_LIB_DIR=/home/bkoz/bin/H-libtorrent/lib64/
make -j$(nproc)