#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::string* s = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    s->append(static_cast<char*>(contents), total);
    return total;
}

std::string http_get(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string buffer;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl error: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return buffer;
}

bool http_download(const std::string& url, const fs::path& out) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    FILE* fp = fopen(out.string().c_str(), "wb");
    if (!fp) { curl_easy_cleanup(curl); return false; }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);
    return res == CURLE_OK;
}

void download_video(const std::string& url, const fs::path& folder) {
    fs::create_directories(folder);
    std::string cmd = "yt-dlp -f best --no-warnings -o '" + folder.string() + "/%(title)s.%(ext)s' '" + url + "'";
    std::system(cmd.c_str());
}

void download_playlist(const std::string& url, const fs::path& folder) {
    fs::create_directories(folder);
    std::string cmd = "yt-dlp -f best --yes-playlist --no-warnings -o '" + folder.string() + "/%(title)s.%(ext)s' '" + url + "'";
    std::system(cmd.c_str());
}

void download_pinterest_image(const std::string& url, const fs::path& folder) {
    fs::create_directories(folder);
    std::string page = http_get(url);
    std::smatch m;
    if (std::regex_search(page, m, std::regex("<img[^>]+src=\"([^\"]+)"))) {
        std::string img_url = m[1];
        std::string filename = fs::path(img_url).filename().string();
        auto pos = filename.find('?');
        if (pos != std::string::npos) filename = filename.substr(0, pos);
        fs::path out = folder / filename;
        if (http_download(img_url, out)) {
            std::cout << "Saved " << out << std::endl;
        } else {
            std::cerr << "Failed to download image" << std::endl;
        }
    } else {
        std::cerr << "Image not found" << std::endl;
    }
}

void download_wb_images(const std::string& url, const fs::path& folder) {
    std::regex r("/catalog/(\\d+)/");
    std::smatch m;
    if (!std::regex_search(url, m, r)) {
        std::cerr << "Cannot parse WB product id" << std::endl;
        return;
    }
    std::string pid = m[1];
    int vol = std::stoi(pid) / 100000;
    int part = std::stoi(pid) / 1000;
    std::string card_data;
    int host_used = -1;
    for (int host = 0; host < 100; ++host) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", host);
        std::string card_url = "https://basket-" + std::string(buf) + ".wbbasket.ru/vol" + std::to_string(vol) + "/part" + std::to_string(part) + "/" + pid + "/info/ru/card.json";
        card_data = http_get(card_url);
        if (!card_data.empty()) { host_used = host; break; }
    }
    if (card_data.empty()) {
        std::cerr << "Failed to fetch card.json" << std::endl;
        return;
    }
    auto json = nlohmann::json::parse(card_data, nullptr, false);
    if (json.is_discarded()) {
        std::cerr << "Invalid JSON" << std::endl;
        return;
    }
    std::string name = json.value("imt_name", "wb_" + pid);
    std::string safe;
    for (char c : name) {
        if (std::string("\\/:*?\"<>|").find(c) == std::string::npos)
            safe.push_back(c);
    }
    fs::path product_folder = folder / safe;
    fs::create_directories(product_folder);
    int count = json["media"].value("photo_count", 0);
    if (count <= 0) {
        std::cerr << "No images" << std::endl;
        return;
    }
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", host_used);
    std::string host_part = "https://basket-" + std::string(buf) + ".wbbasket.ru";
    for (int i = 1; i <= count; ++i) {
        std::string img_url = host_part + "/vol" + std::to_string(vol) + "/part" + std::to_string(part) + "/" + pid + "/images/big/" + std::to_string(i) + ".webp";
        fs::path out = product_folder / (std::to_string(i) + ".webp");
        if (http_download(img_url, out)) {
            std::cout << "Saved " << out << std::endl;
        }
    }
}

void handle_url(const std::string& url, const fs::path& root) {
    std::string u = url;
    auto pos = u.find("://");
    std::string host;
    if (pos != std::string::npos) {
        host = u.substr(pos + 3);
        host = host.substr(0, host.find('/'));
    }
    std::string lc_host;
    for (char c : host) lc_host.push_back(std::tolower(c));

    fs::path downloads = root / "Downloads";
    fs::path videos = downloads / "Videos";
    fs::path playlist = videos / "Playlist Videos";
    fs::path pictures = downloads / "Pictures";
    fs::path wb = pictures / "Wildberries";

    if (url.find("youtube.com/playlist") != std::string::npos) {
        download_playlist(url, playlist);
    } else if (lc_host.find("youtube.com") != std::string::npos || lc_host.find("youtu.be") != std::string::npos) {
        download_video(url, videos);
    } else if (lc_host.find("pinterest.com") != std::string::npos) {
        download_pinterest_image(url, pictures);
    } else if (lc_host.find("wildberries.ru") != std::string::npos) {
        download_wb_images(url, wb);
    } else {
        std::cerr << "Unsupported url: " << url << std::endl;
    }
}

int main() {
    fs::path root = fs::current_path();
    fs::path system_dir = root / "system";
    fs::path list_file = system_dir / "download-list.txt";
    if (!fs::exists(list_file)) {
        std::cerr << "File download-list.txt not found" << std::endl;
        return 1;
    }
    std::ifstream in(list_file);
    std::string url;
    while (std::getline(in, url)) {
        if (url.empty()) continue;
        handle_url(url, root);
    }
    return 0;
}

