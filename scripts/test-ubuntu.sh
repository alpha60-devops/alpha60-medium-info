# Download a known good torrent (Ubuntu ISO)
cd /tmp
wget -O ubuntu.torrent "https://releases.ubuntu.com/22.04/ubuntu-22.04.5-desktop-amd64.iso.torrent"

# Test with our pipeline on this torrent
cd /home/bkoz/src/alpha60-medium-info
./build/media_enrichment /tmp ./test-ubuntu.json ./ubuntu_cache
