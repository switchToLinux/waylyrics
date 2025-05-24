#include "../include/way_lyrics.h"
#include "../include/utils.hpp"
#include "common.h"
#include "player_manager.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <gtk/gtk.h>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

void displayState(const PlayerState &state) {
    DEBUG("Current Player State:");
    DEBUG("  Player Name: %s", state.playerName.c_str());
    DEBUG("  Status: %s", state.status == PlaybackStatus::Playing ? "Playing": "Paused");
    DEBUG("  Position: %10ld ms", state.position);
    DEBUG("  Duration: %10ld ms", state.metadata.length);
    DEBUG("  Metadata:");
    DEBUG("    Title: %s", state.metadata.title.c_str());
    //   DEBUG("    Artist: %s", state.metadata.artist.c_str());
    //   DEBUG("    Lyrics: %s", state.metadata.lyrics.c_str());
    //   DEBUG("    Album: %s", state.metadata.album.c_str());
}

WayLyrics::WayLyrics(const std::string &cacheDir, unsigned int updateInterval,
                     const std::string &cssClass)
    : updateInterval_(updateInterval), cssClass_(cssClass),
      isRunning_(false) {
  // 初始化缓存目录
  cachePath = std::filesystem::path(cacheDir);
  // 初始化D-Bus连接和PlayerManager
  auto dbusUniqueConn = sdbus::createSessionBusConnection();  // 原始unique_ptr连接
  dbusConn_ = std::shared_ptr<sdbus::IConnection>(dbusUniqueConn.release());  // 转换为shared_ptr
  playerManager_ = std::make_unique<PlayerManager>(
      dbusConn_, [this](const PlayerState &state) { // 传递shared_ptr连接
        DEBUG("  >> PlayerState updated: %s", state.playerName.c_str());
        currentState_ = state;
        // 如果歌词为空且状态为播放中，则尝试获取歌词
        if(currentState_.metadata.lyrics.empty() && currentState_.status == PlaybackStatus::Playing){
          try{
            currentState_.metadata.lyrics = getLyrics(currentState_.metadata.title + " " + currentState_.metadata.artist);
          } catch (const std::exception &e) {
            DEBUG("  >> Failed to get lyrics: %s", e.what());
          }
        }
        currentState_.position += 200; // 微调预览歌词的时间
        // displayState(currentState_); // 显示当前状态
      });
  INFO("  >> WayLyrics initialized, params: cacheDir:%s updateInterval=%d, cssClass=%s", cachePath.c_str(), updateInterval_, cssClass_.c_str() );
}

WayLyrics::~WayLyrics() {
  // 先销毁 PlayerManager（释放对 dbusConn_ 的引用）
  playerManager_.reset();
  // 再停止线程和清理资源
  stop(); 
}

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

std::string WayLyrics::getLyrics(const std::string &query) {
  std::string trim_query = query;
  trim_query = trim(trim_query);
  if(trim_query.empty()) {
    return "";
  }
  std::string encoded = url_encode(query);
  std::string url = "https://lrclib.net/api/search?q=" + encoded;

  if (url == currentURL) {
    return currentState_.metadata.lyrics;
  }

  std::filesystem::path lyricsCachePath =
      cachePath / std::string(replace_space(query) + ".txt");
  std::string content;

  std::string syncedLyrics = "";
  if (std::filesystem::exists(lyricsCachePath)) {
    DEBUG("  >> Lyrics found in cache: %s", lyricsCachePath.c_str());
    std::ifstream file(lyricsCachePath, std::ios::binary);
    if (!file.is_open()) {
      ERROR("  >> Failed to open cache file: %s", lyricsCachePath.c_str());
      return "";
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
  } else {
    DEBUG("  >> Lyrics not found in cache[%s], fetching from: %s",
          lyricsCachePath.c_str() , url.c_str());
    CURL *curl = curl_easy_init();
    if (curl) {
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content);
      CURLcode res = curl_easy_perform(curl);

      if (res != CURLE_OK) {
        std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                  << "CURL error: " << curl_easy_strerror(res) << std::endl;
        return "";
      }

      std::thread([lyricsCachePath, content, curl]() {
        try {
            std::fstream file(lyricsCachePath);
            file << content;
            if (file.fail()) {
              ERROR("  >> Failed to write lyrics to cache file: %s",
                    lyricsCachePath.c_str());
              return;
            }
        } catch (const std::exception &e) {
          WARN("  >> Failed to write lyrics to cache file: %s, error: %s",
                lyricsCachePath.c_str(), e.what());
        } catch (...) {
          WARN("  >> Unknown error while writing lyrics to cache file: %s",
                lyricsCachePath.c_str());
        }
        curl_easy_cleanup(curl);
      }).detach();
    }
  }

  try {
    auto json = nlohmann::json::parse(content, nullptr, false);
    if (json.is_discarded())
      return "";

    auto currentLyrics = json.get<std::vector<nlohmann::json>>();
    if (currentLyrics.empty())
      return "";
    auto &first = currentLyrics[0];
    if (first.count("syncedLyrics")) {
      syncedLyrics = first["syncedLyrics"];

      if (!syncedLyrics.empty()) {
        std::thread([lyricsCachePath, syncedLyrics]() {
            std::ofstream file(lyricsCachePath,
                               std::ios::out | std::ios::trunc);
            if (!file.is_open()) {
              ERROR("  >> Failed to open cache file for writing: %s",
                    lyricsCachePath.c_str());
              return;
            }
            file << syncedLyrics;
            if (file.fail()) {
              ERROR("  >> Failed to write lyrics to cache file: %s",
                    lyricsCachePath.c_str());
              return;
            }
            DEBUG("  >> Lyrics cached successfully to: %s", lyricsCachePath.c_str());
        }).detach();
      }
      return syncedLyrics;
    }else {
      WARN("  >> No syncedLyrics found in JSON");
      return "";
    } 
  }catch (const std::exception &e) {
    WARN("Error parsing JSON: %s", e.what());
    return "";
  }
  return "";
}
// 静态方法：提取指定时间戳的歌词行
static std::string getSyncedLine(uint64_t pos, const std::string &syncedLyrics) {
  auto strVec = split(syncedLyrics, "\n");
  auto len = strVec.size();
  DEBUG("  >> getSyncedLine: pos=%ld, len=%ld", pos, len);
  size_t index = 0;
  for (size_t i = 0; i < len; i++) {
    auto &cur = strVec[i];
    if (cur.empty()) {
      continue;
    }
    uint64_t ms = timestampToMs(cur);
    if (pos > ms) {
        index = i;
    } else {
      break;
    }
  }
  DEBUG("  >> getSyncedLine: index=%ld, len:%ld , line:[%s]", index, len, index >= len ? "out of size": strVec[index].c_str());
  if (index >= len) {
    return "";
  }
  auto str = strVec[index];
  const size_t time_len = 10; // 时间戳长度，例如 "[00:00.00]xxxx"
  if (str.size() <= time_len) {
    return "";
  }
  str = str.substr(time_len, str.size());
  return trim(str);
}

static void updateLabelText(GtkLabel *label, const std::string &text, uint64_t position, std::string prefix = "") {
  // 使用 gdk_threads_add_idle 提交到主线程执行
  auto line = getSyncedLine(position, text);

  line = prefix + line;
  DEBUG("  >> Updating label: positon:%ld, text: %s", position, line.c_str());
  gdk_threads_add_idle(
      [](gpointer data) -> gboolean {
        auto *args = static_cast<std::pair<GtkLabel *, std::string> *>(data);
        // 检查标签是否存活（GTK_IS_LABEL 宏）
        if (GTK_IS_LABEL(args->first)) {
          gtk_label_set_text(args->first, args->second.c_str());
        }
        delete args;  // 释放临时数据
        return FALSE; // 只执行一次
      },
      new std::pair<GtkLabel *, std::string>(label, line)); // 传递标签和文本
}

void WayLyrics::start(GtkLabel *label) {
  if (isRunning_)
    return;
  displayLabel_ = label;
  isRunning_ = true;

  INFO(">> WayLyrics started");
  // 应用CSS样式
  auto context = gtk_widget_get_style_context(GTK_WIDGET(label));
  gtk_style_context_add_class(context, cssClass_.c_str());

  // 如果歌词为空且状态为播放中，则尝试获取歌词
  if (currentState_.metadata.lyrics.empty() &&
      currentState_.status == PlaybackStatus::Playing) {
    currentState_.metadata.lyrics = getLyrics(currentState_.metadata.title + " " + currentState_.metadata.artist);
  }
  // 初始化时显示初始歌词
  std::string prefix = "";
  if(!currentState_.metadata.title.empty())
    prefix = "《" + currentState_.metadata.title + "》" + currentState_.metadata.artist + " - ";

  if (!currentState_.metadata.lyrics.empty()) {
    updateLabelText(displayLabel_, currentState_.metadata.lyrics,
                    currentState_.position, prefix);
  } else if(currentState_.playerName.empty()) {
    updateLabelText(displayLabel_, NOPLAYER, 0, "");
  } else {
    updateLabelText(displayLabel_, "", 0, "loading...");
  }

  INFO("  >> Starting update thread");
  updateThread_ = std::thread([this]() {
      try {
        while (isRunning_) {
          DEBUG("  >> Update thread started");
          std::string prefix = "";
          std::string lyricsLine = "";
          if (currentState_.playerName.empty()) {
            lyricsLine = NOPLAYER;
          } else if (currentState_.metadata.lyrics.empty()) {
            lyricsLine = "no lyrics...";
          } else if(currentState_.status != PlaybackStatus::Playing) {
            lyricsLine = "paused...";
            if(!currentState_.metadata.title.empty()) {
              prefix = "《" + currentState_.metadata.title + "》" +
                       currentState_.metadata.artist + " - ";
            }
          } else {
            if(!currentState_.metadata.title.empty()) {
                prefix = "《" + currentState_.metadata.title + "》" +
                           currentState_.metadata.artist + " - ";
            } else {
              prefix = "[unknown title] - ";
            }
            lyricsLine = currentState_.metadata.lyrics;
          }
          updateLabelText(displayLabel_, currentState_.metadata.lyrics,
                        currentState_.position, prefix);
          // 短间隔睡眠并检查 isRunning_，减少退出延迟
          for (unsigned int i = 0; i < updateInterval_ && isRunning_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (isRunning_ && currentState_.status == PlaybackStatus::Playing) {
              currentState_.position += 1000;
            }
          }
        }
    } catch (const std::exception &e) {
      WARN("  >> Update thread error: %s", e.what());
    } catch (...) {
      WARN("  >> Unknown error in update thread");
    }
  });
}

void WayLyrics::stop() {
  if (!isRunning_) return;
  isRunning_ = false;
  // 主动等待线程退出（避免 sleep_for 延迟）
  if (updateThread_.joinable()) {
    // 等待线程退出（最多等待 1 秒，避免死锁）
    if (updateThread_.joinable()) {
      updateThread_.join();
    }
  }
  // 确保所有 UI 任务执行完毕后再销毁标签
  gdk_threads_add_idle([](gpointer data) {
    return FALSE;
  }, nullptr);
  displayLabel_ = nullptr;  // 此时 UI 任务已无引用
}

void WayLyrics::toggle() { isRunning_ ? stop() : start(displayLabel_); }

bool WayLyrics::isRunning() const { return isRunning_; }

void WayLyrics::nextPlayer() {
  // 切换到下一个播放器（简化实现）
  auto players = playerManager_->getAllPlayers();
  auto current = playerManager_->getCurrentPlayerName();
  auto it = std::find(players.begin(), players.end(), current);
  if (it != players.end()) {
    playerManager_->setCurrentPlayer((it + 1) == players.end() ? players[0]
                                                               : *(it + 1));
  }
}

void WayLyrics::prevPlayer() {
  // 切换到上一个播放器（简化实现）
  auto players = playerManager_->getAllPlayers();
  auto current = playerManager_->getCurrentPlayerName();
  auto it = std::find(players.begin(), players.end(), current);
  if (it != players.end()) {
    playerManager_->setCurrentPlayer(it == players.begin() ? players.back()
                                                           : *(it - 1));
  }
}

std::string WayLyrics::getCurrentPlayer() const {
  return playerManager_->getCurrentPlayerName();
}