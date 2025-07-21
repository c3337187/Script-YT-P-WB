# Script-YT-P-WB

This repository originally contained a Python script for downloading media from various services. A simplified C++ version is provided as `main.cpp` which downloads items listed in `system/download-list.txt` using `yt-dlp` and `libcurl`.

## Building

```
g++ -std=c++17 main.cpp -o downloader -lcurl
```

Make sure `yt-dlp` is installed and available in your `PATH`.
