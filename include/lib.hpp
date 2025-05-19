#include <filesystem>
#include <fstream>

#include <iostream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sdbus-c++/sdbus-c++.h>
#include <thread>

#include "utils.hpp"

const auto cacheDir = []() {
  const char *home = getenv("HOME");
  return std::filesystem::path(std::string(home) + "/.cache/waylyrics");
}();

inline const char *loadingText = (char *)"loading lyrics...";
inline const char *panicText = (char *)"no lyrics...";

inline std::unique_ptr<sdbus::IConnection> conn;
inline std::unique_ptr<sdbus::IProxy> proxy;
inline std::string currentURL;
inline std::vector<nlohmann::json> currentLyrics;

inline void init() {
  static bool has_run = false;
  if (has_run) {
    return;
  }
  has_run = true;

  std::filesystem::create_directories(cacheDir);
  try {
    conn = sdbus::createSessionBusConnection();
    sdbus::ServiceName destination{"org.mpris.MediaPlayer2.mpv"};
    sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
    proxy = sdbus::createProxy(*conn, destination, objectPath);
    std::cout << "Connected to D-Bus" << std::endl;
  } catch (const sdbus::Error &e) {
    std::cerr << "D-Bus error: " << e.getMessage() << std::endl;
  }
}

inline std::tuple<std::string, std::string, int, int, bool> getNowPlaying() {
  if (!proxy) {
    std::cerr << "D-Bus proxy not initialized" << std::endl;
    return {"", "", 0, 0, false};
  }

  int64_t position = 0;
  std::string title;
  std::vector<std::string> artists;
  int64_t length = 0;

  try {
    // 1. 获取播放状态
    sdbus::Variant playbackStatus;
    proxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "PlaybackStatus")
        .storeResultsTo(playbackStatus);

    const std::string status = playbackStatus.get<std::string>();

    if (status != "Playing") {
      std::cerr << "Not in playing state: " << status << std::endl;
      return {status, "", 0, 0, false}; // Paused 或 Playing 状态
    }
  } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
    std::cerr << "D-Bus error0: " << e.getMessage() << std::endl;
    return {"Stoped", "", 0, 0, false}; // 没有找到 mpv 实例
  }
  try {
    // 2. 获取媒体元数据
    sdbus::Variant metadata;
    proxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "Metadata")
        .storeResultsTo(metadata);

    auto md = metadata.get<std::map<std::string, sdbus::Variant>>();

    // 解析标题
    if (md.count("xesam:title")) {
      title = md["xesam:title"].get<std::string>();
    } else {
      std::cerr << "Warning: xesam:title not found in metadata" << std::endl;
    }

    if (md.count("xesam:artist")) {
      artists = md["xesam:artist"].get<std::vector<std::string>>();
    } else {
      // 如果没有找到 xesam:artist，尝试使用 xesam:albumArtist （array)
      if (md.count("xesam:albumArtist")) {
        artists = md["xesam:albumArtist"].get<std::vector<std::string>>();
      } else {
        std::cerr << "Warning: xesam:albumArtist not found in metadata"
                  << std::endl;
      }
    }

    // 解析媒体长度
    if (md.count("mpris:length")) {
      // 数据类型可能是 int64 或 uint64，统一转为 int64
      length = md["mpris:length"].get<int64_t>();
    } else {
      std::cerr << "Warning: mpris:length not found in metadata" << std::endl;
    }
  } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
    std::cerr << "D-Bus error1: " << e.getMessage() << std::endl;
    return {"", "", 0, 0, false};
  } catch (const std::exception &e) { // 捕获其他标准异常
    std::cerr << "General error1: " << e.what() << std::endl;
    return {"", "", 0, 0, false};
  }

  try {

    // 3. 获取播放位置
    sdbus::Variant posVar;
    proxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "Position")
        .storeResultsTo(posVar);
    position = posVar.get<int64_t>();
  } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
    std::cerr << "D-Bus error2: " << e.getMessage() << std::endl;
    return {"", "", 0, 0, false};
  } catch (const std::exception &e) { // 捕获其他标准异常
    std::cerr << "General error2: " << e.what() << std::endl;
    return {"", "", 0, 0, false};
  }

  return {title, artists.empty() ? "" : artists[0],
          static_cast<int>(position / 1000), // 转换为毫秒（原单位是微秒）
          static_cast<int>(length / 1000),   // 转换为毫秒（原单位是微秒）
          true};
}

inline size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

inline std::vector<nlohmann::json> getLyrics(const std::string &query) {
  std::string encoded = url_encode(query);
  std::string url = "https://lrclib.net/api/search?q=" + encoded;

  if (url == currentURL) {
    return currentLyrics;
  }

  std::filesystem::path cachePath = cacheDir / std::to_string(hash_fnv(url));
  std::string content;

  if (std::filesystem::exists(cachePath)) {
    std::ifstream file(cachePath, std::ios::binary);
    content = std::string(std::istreambuf_iterator<char>(file), {});
  } else {
    CURL *curl = curl_easy_init();
    if (curl) {
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content);
      CURLcode res = curl_easy_perform(curl);

      if (res != CURLE_OK) {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
        return {};
      }

      std::thread([cachePath, content, curl]() {
        std::ofstream file(cachePath);
        file << content;
        curl_easy_cleanup(curl);
      }).detach();
    }
  }

  try {
    auto json = nlohmann::json::parse(content, nullptr, false);
    if (json.is_discarded())
      return {};

    currentLyrics = json.get<std::vector<nlohmann::json>>();
  } catch (const std::exception &e) {
    std::cerr << "Error parsing JSON: " << e.what() << std::endl;
    return {};
  }

  return currentLyrics;
}
inline std::string getSyncedLine(uint64_t pos,
                                 const std::string &syncedLyrics) {
  auto strVec = split(syncedLyrics, "\n");
  auto len = strVec.size();

  size_t index = 0;
  for (size_t i = 0; i < len; i++) {
    auto &cur = strVec[i];
    auto start = cur.find("[");
    auto mins_s = std::string_view(cur.data() + start + 1, 2);
    auto secs_s = std::string_view(cur.data() + start + 4, 5);

    auto secs = (atoi(mins_s.data()) * 60) + atof(secs_s.data());
    auto ms = secs * 1000;
    if (pos > ms) {
      index = i;
    }
  }

  auto str = strVec[index];
  str = str.substr(10, str.size());
  return trim(str);
}

inline std::string getPlainLine(uint64_t pos, uint64_t dur,
                                const std::string &plainLyrics) {
  auto strVec = split(plainLyrics, "\n");
  return strVec[size_t(pos * strVec.size() / dur)];
}

inline std::optional<std::tuple<std::string, int64_t, int64_t>>
getCurrentLine() {
  std::string line = panicText;
  auto [title, artists, pos, dur, ok] = getNowPlaying();
  if (!ok) {                             // 非 playing 状态
    return std::make_tuple(title, 0, 0); // 此时 title 表示的状态信息
  }
  auto ret = getLyrics(title + ' ' + artists);
  if (ret.size()) {
    try {
      auto &first = ret[0];
      if (first.count("syncedLyrics")) {
        std::string syncedLyrics = first["syncedLyrics"];
        line = getSyncedLine(pos, syncedLyrics);
      } else if (first.count("plainLyrics")) {
        std::string plainLyrics = first["plainLyrics"];
        line = getPlainLine(pos, dur, plainLyrics);
      } else {
        std::cerr << "No lyrics for item" << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "Error processing lyrics json: " << e.what() << std::endl;
    }
  } else {
    std::cerr << "No lyrics list for [" << title << artists << pos << dur << ok
              << "]" << std::endl;
  }
  return std::make_tuple(line, pos, dur);
}
