#include "utils.hpp"
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <thread>
#include <vector>

// 定义缓存目录
const auto cacheDir = []() {
  const char *home = getenv("HOME");
  return std::filesystem::path(std::string(home) + "/.cache/waylyrics");
}();

// 定义加载和无歌词提示文本
inline const char *loadingText = "loading lyrics...";
inline const char *panicText = "no lyrics...";

// 全局变量，用于存储D-Bus连接、代理、当前URL和当前歌词
inline std::unique_ptr<sdbus::IConnection> conn;
inline std::unique_ptr<sdbus::IProxy> dbusProxy;
inline std::string currentURL;
inline std::vector<nlohmann::json> currentLyrics;

// D-Bus服务和路径常量
const std::string DBUS_SERVICE = "org.freedesktop.DBus";
const std::string DBUS_PATH = "/org/freedesktop/DBus";
const std::string MPRIS_PREFIX = "org.mpris.MediaPlayer2.";

// 初始化D-Bus连接和代理
inline void initDBus() {
  static bool has_run = false;
  if (has_run) {
    return;
  }
  has_run = true;

  // 创建缓存目录
  std::filesystem::create_directories(cacheDir);
  try {
    // 创建会话总线连接
    conn = sdbus::createSessionBusConnection();
    if (!conn) {
      std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Failed to create D-Bus connection" << std::endl;
      return;
    }
    std::cout << "Connected to D-Bus" << std::endl;

    // 创建D-Bus代理
    sdbus::ServiceName destination{DBUS_SERVICE};
    sdbus::ObjectPath objectPath{DBUS_PATH};
    dbusProxy = sdbus::createProxy(*conn, destination, objectPath);
    if (!dbusProxy) {
      std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Failed to create D-Bus proxy" << std::endl;
      return;
    }
    std::cout << "Created D-Bus proxy" << std::endl;
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "D-Bus error: " << e.getMessage() << std::endl;
  }
}

// 查找正在播放的播放器服务名
inline std::string findPlayingPlayer(const std::string &filterDest) {
  if (!conn) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "[findPlayingPlayer] D-Bus connection not initialized"
              << std::endl;
    return "";
  }

  if (!dbusProxy) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "[findPlayingPlayer] D-Bus proxy not initialized"
              << std::endl;
    return "";
  }
  sdbus::Variant namesVariant;
  try {
    // 获取所有注册服务名
    dbusProxy->callMethod("ListNames")
        .onInterface(DBUS_SERVICE)
        .storeResultsTo(namesVariant);
    std::cerr << namesVariant.get<std::vector<std::string>>().size() << std::endl;
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "D-Bus error: " << e.getMessage() << std::endl;
    return "";
  } catch (const std::exception &e) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "General error: " << e.what() << std::endl;
    return "";
  }
  try{
    auto names = namesVariant.get<std::vector<std::string>>();
    // 遍历服务名，查找正在播放的播放器
    for (const auto &name : names) {
      if (name.find(MPRIS_PREFIX) == 0) {
        if (filterDest.empty() || name.find(filterDest) != std::string::npos) {
          sdbus::Variant playbackStatus;
          std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "[findPlayingPlayer] try to get player: " << name << std::endl;
          // 创建播放器代理
          sdbus::ServiceName destination{name};
          sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
          auto playerProxy = sdbus::createProxy(*conn, destination, objectPath);
          if (!playerProxy) {
            std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Failed to create player proxy for " << name << std::endl;
            continue;
          }
          std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "[findPlayingPlayer] created player proxy for " << name << std::endl;

          playerProxy->callMethod("Get")
              .onInterface("org.freedesktop.DBus.Properties")
              .withArguments("org.mpris.MediaPlayer2.Player", "PlaybackStatus")
              .storeResultsTo(playbackStatus);

          const std::string status = playbackStatus.get<std::string>();
          if (status == "Playing") {
            std::cout << "Found matching player: " << name << std::endl;
            return name;
          }
        }
      }
    }
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "D-Bus error: " << e.getMessage() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "General error: " << e.what() << std::endl;
  }

  std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "No matching player found" << std::endl;
  return "";
}

// 获取正在播放的媒体元数据
inline std::tuple<std::string, std::string, int, int, bool>
getMediaMetadata(const std::string &playerService) {
  int64_t position = 0;
  std::string title;
  std::vector<std::string> artists;
  int64_t length = 0;

  try {
    // 为目标播放器创建专用代理
    sdbus::ServiceName destination{playerService};
    sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
    auto playerProxy = sdbus::createProxy(*conn, destination, objectPath);

    // 获取播放状态
    sdbus::Variant playbackStatus;
    playerProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments(playerService, "PlaybackStatus")
        .storeResultsTo(playbackStatus);
    const std::string status = playbackStatus.get<std::string>();
    if (status != "Playing") {
      std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Not in playing state: " << status << std::endl;
      return {"", "", 0, 0, false};
    }

    // 获取媒体元数据
    sdbus::Variant metadata;
    playerProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments(playerService, "Metadata")
        .storeResultsTo(metadata);
    auto md = metadata.get<std::map<std::string, sdbus::Variant>>();

    // 解析标题
    if (md.count("xesam:title")) {
      title = md["xesam:title"].get<std::string>();
    } else {
      std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Warning: xesam:title not found in metadata" << std::endl;
    }

    // 解析艺术家
    if (md.count("xesam:artist")) {
      artists = md["xesam:artist"].get<std::vector<std::string>>();
    } else if (md.count("xesam:albumArtist")) {
      artists = md["xesam:albumArtist"].get<std::vector<std::string>>();
    } else {
      std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Warning: xesam:albumArtist not found in metadata"
                << std::endl;
    }

    // 解析媒体长度
    if (md.count("mpris:length")) {
      length = md["mpris:length"].get<int64_t>();
    } else {
      std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Warning: mpris:length not found in metadata" << std::endl;
    }

    // 获取播放位置
    sdbus::Variant posVar;
    playerProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments(playerService, "Position")
        .storeResultsTo(posVar);
    position = posVar.get<int64_t>();
  } catch (const sdbus::Error &e) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "D-Bus error: " << e.getMessage() << std::endl;
    return {"", "", 0, 0, false};
  } catch (const std::exception &e) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "General error: " << e.what() << std::endl;
    return {"", "", 0, 0, false};
  }

  return {title, artists.empty() ? "" : artists[0],
          static_cast<int>(position / 1000), static_cast<int>(length / 1000),
          true};
}

// CURL回调函数，用于将响应数据追加到字符串中
inline size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

// 获取歌词
inline std::vector<nlohmann::json> getLyrics(const std::string &query) {
  std::string encoded = url_encode(query);
  std::string url = "https://lrclib.net/api/search?q=" + encoded;

  if (url == currentURL) {
    return currentLyrics;
  }

  std::filesystem::path cachePath = cacheDir / std::to_string(hash_fnv(url));
  std::string content;

  // 检查缓存文件是否存在
  if (std::filesystem::exists(cachePath)) {
    std::ifstream file(cachePath, std::ios::binary);
    content = std::string(std::istreambuf_iterator<char>(file), {});
  } else {
    // 发起HTTP请求获取歌词
    CURL *curl = curl_easy_init();
    if (curl) {
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content);
      CURLcode res = curl_easy_perform(curl);

      if (res != CURLE_OK) {
        std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "CURL error: " << curl_easy_strerror(res) << std::endl;
        return {};
      }

      // 保存响应数据到缓存文件
      std::thread([cachePath, content, curl]() {
        std::ofstream file(cachePath);
        file << content;
        curl_easy_cleanup(curl);
      }).detach();
    }
  }

  try {
    // 解析JSON数据
    auto json = nlohmann::json::parse(content, nullptr, false);
    if (json.is_discarded())
      return {};

    currentLyrics = json.get<std::vector<nlohmann::json>>();
  } catch (const std::exception &e) {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Error parsing JSON: " << e.what() << std::endl;
    return {};
  }

  return currentLyrics;
}

// 获取同步歌词的当前行
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

// 获取纯文本歌词的当前行
inline std::string getPlainLine(uint64_t pos, uint64_t dur,
                                const std::string &plainLyrics) {
  auto strVec = split(plainLyrics, "\n");
  return strVec[size_t(pos * strVec.size() / dur)];
}

// 获取当前行歌词
inline std::optional<std::tuple<std::string, int64_t, int64_t>>
getCurrentLine(const char *filterDest) {
  std::string line = panicText;

  // 查找正在播放的播放器
  std::string playerService = findPlayingPlayer(filterDest ? filterDest : "");
  if (playerService.empty()) {
    return std::make_tuple("", 0, 0);
  }

  // 获取媒体元数据
  auto [title, artists, pos, dur, ok] = getMediaMetadata(playerService);
  if (!ok) {
    return std::make_tuple(title, 0, 0);
  }

  // 获取歌词
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
        std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "No lyrics for item" << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "Error processing lyrics json: " << e.what() << std::endl;
    }
  } else {
    std::cerr << __FILE__ << ":" << __func__ <<":" << __LINE__ << " :" << "No lyrics list for [" << title << " " << artists << " " << pos
              << " " << dur << " " << ok << "]" << std::endl;
  }

  return std::make_tuple(line, pos, dur);
}
