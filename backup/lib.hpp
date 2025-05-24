#include <filesystem>
#include <fstream>

#include <iostream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include <vector>

#include "utils.hpp"



const std::filesystem::path defaultCacheDir = []() {
  const char *home = getenv("HOME");
  return std::filesystem::path(std::string(home) + "/.cache/waylyrics");
}();

inline std::filesystem::path cachePath;

inline const char *loadingText = "loading lyrics...";
// inline const char *panicText = "no lyrics...";

inline std::unique_ptr<sdbus::IConnection> conn;
inline std::unique_ptr<sdbus::IProxy> proxy;
inline std::unique_ptr<sdbus::IProxy> dbusProxy;
inline std::string currentURL;
inline std::vector<nlohmann::json> currentLyrics;

inline std::vector<std::string> listPlayerNames();
inline std::string getPlayingName(const std::vector<std::string> &names);

inline void init(std::string destName, std::string cacheDir) {
  static bool has_run = false;
  if (has_run) {
    return;
  }
  has_run = true;
  std::string serviceName;

  try {
    conn = sdbus::createSessionBusConnection();
    if (destName == "mpris") {
      // mpris player name
      sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
      sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
      dbusProxy = sdbus::createProxy(*conn, org_freedesktop_DBus,
                                      org_freedesktop_DBus_path);
      auto names = listPlayerNames();
      if (names.empty()) {
        std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "No MPRIS player found"
                  << std::endl;
        return;
      } else {
        std::cout << "Found MPRIS players: " << std::endl;
        for (const auto &name : names) {
          std::cout << name << std::endl;
        }
      }
    }
    sdbus::ServiceName destination{"org.mpris.MediaPlayer2.mpv"}; // TODO: 支持其他播放器
    sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
    proxy = sdbus::createProxy(*conn, destination, objectPath);
    std::cout << "Connected to D-Bus" << std::endl;
    if (cacheDir.empty()) {
      std::cout << "Cache dir not specified, use default: " << defaultCacheDir << std::endl;
      cachePath = defaultCacheDir;
    } else {
      cachePath = std::filesystem::path(cacheDir);
    }
    std::filesystem::create_directories(cachePath);
    std::cout << "Cache dir created: " << cachePath << std::endl;
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus error: " << e.getMessage() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "General error: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Unknown error occurred" << std::endl;
  }
}

inline std::vector<std::string> listPlayerNames() {
  if (!dbusProxy) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus proxy not initialized" << std::endl;
    return {};
  }
  std::vector<std::string> names;
  try {
    dbusProxy->callMethod("ListNames")
        .onInterface("org.freedesktop.DBus")
        .storeResultsTo(names);
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus error: " << e.getMessage() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "General error: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Unknown error occurred" << std::endl;
  }
  // 过滤出以 org.mpris.MediaPlayer2. 开头的名称 并且过滤掉 playerctld 结尾的名称
  names.erase(std::remove_if(names.begin(), names.end(),
                             [](const std::string &name) {
                               return name.find("org.mpris.MediaPlayer2.")!= 0 || name.find("playerctld")!= std::string::npos;
                             }),
              names.end());
  return names;
}

// 根据names获取Playing状态的name
inline std::string getPlayingName(const std::vector<std::string> &names) {
  if (!dbusProxy) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus proxy not initialized" << std::endl;
    return "";
  }
  sdbus::Variant playbackStatus;
  for (const auto &name : names) {
    try {
      sdbus::ServiceName destination{name};
      sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
      proxy = sdbus::createProxy(*conn, destination, objectPath);
      proxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments(name, "PlaybackStatus")
          .storeResultsTo(playbackStatus);

      const std::string status = playbackStatus.get<std::string>();
      if (status == "Playing") {
        std::cout << "Found playing player: " << name << std::endl;
        return name;
      }
    } catch (const sdbus::Error &e) {
      std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus error: " << e.getMessage() << std::endl;
    }
  }
  return "";
}

inline std::tuple<std::string, std::string, int, int, bool> getNowPlaying() {
  if (!proxy) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus proxy not initialized" << std::endl;
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
      std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Not in playing state: " << status << std::endl;
      return {status, "", 0, 0, false}; // Paused 或 Playing 状态
    }
  } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus error0: " << e.getMessage() << std::endl;
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
      std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Warning: xesam:title not found in metadata" << std::endl;
    }

    if (md.count("xesam:artist")) {
      artists = md["xesam:artist"].get<std::vector<std::string>>();
    } else {
      // 如果没有找到 xesam:artist，尝试使用 xesam:albumArtist （array)
      if (md.count("xesam:albumArtist")) {
        artists = md["xesam:albumArtist"].get<std::vector<std::string>>();
      } else {
        std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Warning: xesam:albumArtist not found in metadata"
                  << std::endl;
      }
    }
    // 解析媒体长度
    if (md.count("mpris:length")) {
      // 数据类型可能是 int64 或 uint64，统一转为 int64
      length = md["mpris:length"].get<int64_t>();
    } else {
      std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Warning: mpris:length not found in metadata" << std::endl;
    }
  } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus error1: " << e.getMessage() << std::endl;
    return {"", "", 0, 0, false};
  } catch (const std::exception &e) { // 捕获其他标准异常
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "General error1: " << e.what() << std::endl;
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
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "D-Bus error2: " << e.getMessage() << std::endl;
    return {"", "", 0, 0, false};
  } catch (const std::exception &e) { // 捕获其他标准异常
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "General error2: " << e.what() << std::endl;
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

  std::filesystem::path lyricsCachePath = cachePath / std::to_string(hash_fnv(url));
  std::string content;

  if (std::filesystem::exists(lyricsCachePath)) {
    std::ifstream file(lyricsCachePath, std::ios::binary);
    content = std::string(std::istreambuf_iterator<char>(file), {});
  } else {
    CURL *curl = curl_easy_init();
    if (curl) {
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content);
      CURLcode res = curl_easy_perform(curl);

      if (res != CURLE_OK) {
        std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "CURL error: " << curl_easy_strerror(res) << std::endl;
        return {};
      }

      std::thread([lyricsCachePath, content, curl]() {
        std::ofstream file(lyricsCachePath);
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
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Error parsing JSON: " << e.what() << std::endl;
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
  std::string line = "";
  auto [title, artists, pos, dur, ok] = getNowPlaying();
  if (!ok) {                             // 非 playing 状态
    return std::make_tuple(title, 0, 0); // 此时 title 表示的状态信息
  }
  std::vector<nlohmann::json> ret;
  try {
    ret = getLyrics(title);
  } catch (const std::exception &e) {
    std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Error getting lyrics: " << e.what() << std::endl;
    return std::make_tuple(title, 0, 0);
  }
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
        std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "No lyrics for item" << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << __FILE__ <<":" << __func__ <<":" << __LINE__ <<":"<< "Error processing lyrics json: " << e.what() << std::endl;
    }
  } else {
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "No lyrics for [" << title << "|" << artists << "|" << pos
              << "|" << dur << "|" << ok << "]" << std::endl;
  }
  std::string lineWithTitle = "《" + title + "》-" + artists ;
  if (!line.empty()) {
    lineWithTitle += ":" + line;
  }
  return std::make_tuple(lineWithTitle, pos, dur);
}
