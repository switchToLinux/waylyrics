#include "../include/player_manager.h"
#include "common.h"
#include <cstddef>
#include <iostream>
#include <mutex>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <utility>


PlayerManager::PlayerManager(
    std::shared_ptr<sdbus::IConnection> dbusConn,
    std::function<void(const PlayerState &)> stateCallback)
    : dbusConn_(std::move(dbusConn)), stateCallback_(std::move(stateCallback)) {
  if (!dbusConn_) {
    std::cerr << "Failed to initialize D-Bus connection" << std::endl;
    return;
  }
  sdbus::ServiceName destination{"org.freedesktop.DBus"};
  sdbus::ObjectPath objectPath{"/org/freedesktop/DBus"};
  dbusProxy_ = sdbus::createProxy(*dbusConn_, destination, objectPath);
  if (!dbusProxy_) {
    std::cerr << "Failed to create D-Bus proxy" << std::endl;
    return;
  }
  INFO("D-Bus connection initialized");
  startMonitoring(); // 初始化时自动启动监听
}

PlayerManager::~PlayerManager() {
  stopMonitoring(); // 析构时停止监听
}

// 切换到下一个播放器，如果没有播放器可用，则返回空字符串
std::string PlayerManager::switchNewPlayer() const {
  auto allPlayer = getAllPlayers();
  if (allPlayer.empty()) {
    return "";
  }
  // 找到当前播放器在列表中的索引
  auto currentIndex = std::find(allPlayer.begin(), allPlayer.end(), currentPlayer_);
  if (currentIndex == allPlayer.end()) {
    DEBUG("Current player not found in player list, using first player: %s", allPlayer[0].c_str());
    return allPlayer[0];
  }
  // 计算下一个播放器的索引
  size_t nextIndex = (currentIndex - allPlayer.begin() + 1) % allPlayer.size();
  DEBUG("Switching to next player: %s", allPlayer[nextIndex].c_str());
  return allPlayer[nextIndex];
}

std::vector<std::string> PlayerManager::listPlayerNames() {
  std::vector<std::string> playerNames;
  try {
    // 连接到会话总线
    auto connection = sdbus::createSessionBusConnection();

    // 创建 D-Bus 代理以访问 org.freedesktop.DBus 服务
    sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
    sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
    auto dbusProxy = sdbus::createProxy(*connection, 
      org_freedesktop_DBus,
      org_freedesktop_DBus_path);

    std::vector<std::string> allNames;
    dbusProxy->callMethod("ListNames")
        .onInterface("org.freedesktop.DBus")
        .storeResultsTo(allNames);

    for (const auto &name : allNames) {
      if (name.find("org.mpris.MediaPlayer2.") == 0 && name.find("playerctld") == std::string::npos) {
        playerNames.push_back(name);
      }
    }
  } catch (const sdbus::Error &e) {
    std::cerr << "Error getting player names: " << e.what() << std::endl;
  }
  return playerNames;
}

PlayerState PlayerManager::getPlayerState() const {
  PlayerState state = {PlaybackStatus::Stopped, {}, 0, currentPlayer_};
  if(currentPlayer_.empty()) {
    return state;
  }
  sdbus::ServiceName destination{std::move(currentPlayer_)};
  sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
  auto playerProxy = sdbus::createProxy(*dbusConn_, std::move(destination),
                                        std::move(objectPath));

  // 1. 获取播放状态
  try {
    sdbus::Variant playbackStatus;
    playerProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "PlaybackStatus")
        .storeResultsTo(playbackStatus);

    const std::string status = playbackStatus.get<std::string>();

    if (status != "Playing") {
      state.status = PlaybackStatus::Paused;
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Not in playing state: " << status << std::endl;
      return state;
    } else {
      state.status = PlaybackStatus::Playing;
    }
  } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "D-Bus error0: " << e.getMessage() << std::endl;
    return state;
  }
  // 2. 获取媒体元数据
  try {
    sdbus::Variant metadata;
    playerProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "Metadata")
        .storeResultsTo(metadata);

    auto md = metadata.get<std::map<std::string, sdbus::Variant>>();

    // 解析标题
    if (md.count("xesam:title")) {
      state.metadata.title = md["xesam:title"].get<std::string>();
    } else {
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Warning: xesam:title not found in metadata" << std::endl;
    }

    // 歌词解析
    if (md.count("xesam:asText")) {
      state.metadata.lyrics = md["xesam:asText"].get<std::string>();
    } else {
      // TODO: 网络获取歌词信息或者查询本地缓存
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Warning: xesam:asText not found in metadata" << std::endl;
    }

    if (md.count("xesam:artist")) {
      state.metadata.artist =
          md["xesam:artist"].get<std::vector<std::string>>()[0];
    } else {
      // 如果没有找到 xesam:artist，尝试使用 xesam:albumArtist （array)
      if (md.count("xesam:albumArtist")) {
        state.metadata.artist =
            md["xesam:albumArtist"].get<std::vector<std::string>>()[0];
      } else {
        std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                  << "Warning: xesam:albumArtist not found in metadata"
                  << std::endl;
      }
    }
    // 解析媒体长度
    if (md.count("mpris:length")) {
      // 数据类型是 int64_t
      state.metadata.length = md["mpris:length"].get<int64_t>() / 1000;
    } else {
      std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
                << "Warning: mpris:length not found in metadata" << std::endl;
    }
  } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "D-Bus error1: " << e.getMessage() << std::endl;
    return state;
  } catch (const std::exception &e) { // 捕获其他标准异常
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "General error1: " << e.what() << std::endl;
    return state;
  }

  // 3. 获取播放位置
  try {
    sdbus::Variant posVar;
    playerProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "Position")
        .storeResultsTo(posVar);
    state.position = posVar.get<int64_t>() / 1000;
  } catch (const sdbus::Error &e) { // 捕获D-Bus特定错误
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "D-Bus error2: " << e.getMessage() << std::endl;
    return state;
  } catch (const std::exception &e) { // 捕获其他标准异常
    std::cerr << __FILE__ << ":" << __func__ << ":" << __LINE__ << ":"
              << "General error2: " << e.what() << std::endl;
    return state;
  }
  return state;
}
// 启动D-Bus信号监听（NameOwnerChanged）
// 初始化当前活跃的播放器列表
// 启动事件循环
void PlayerManager::startMonitoring() {
  if (!dbusProxy_)
    return;

  // 初始化当前活跃的播放器列表
  for (const auto &name : listPlayerNames()) {
    INFO("Found player: %s", name.c_str());
    addNewPlayer(name); // 新播放器启动
  }
  updatePlayerState();
  DEBUG("Current player: [%s]", currentPlayer_.c_str());
  INFO("Starting D-Bus signal monitoring");
  // 注册NameOwnerChanged信号监听器
  dbusProxy_->uponSignal("NameOwnerChanged")
      .onInterface("org.freedesktop.DBus")
      .call([this](const std::string &name, const std::string &oldOwner,
                   const std::string &newOwner) {
        if (name.find("org.mpris.MediaPlayer2.") != 0)
          return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (newOwner.empty()) {
          // 播放器退出：从管理列表移除
          players_.erase(name);
          if (name == currentPlayer_) {
            currentPlayer_ = switchNewPlayer();
          }
          updatePlayerState();
          INFO("Player exited: %s", name.c_str());
        } else if (oldOwner.empty()) {
          INFO("New player detected: %s", name.c_str());
          addNewPlayer(name);
        }
      });
  // 启动事件循环
  INFO("Starting D-Bus event loop");
  eventLoopThread_ = std::thread([this]() { dbusConn_->enterEventLoop(); });
}

void PlayerManager::stopMonitoring() {
  // 遍历所有播放器代理，移除信号监听器
  for (auto &[serviceName, playerProxy] : players_) {
    playerProxy->unregister();
  }
  players_.clear();
  dbusConn_->leaveEventLoop();
}
// 新增：Metadata 解析函数（实现）
void PlayerManager::parseMetadata(
    const std::map<std::string, sdbus::Variant> &metadata,
    PlayerMetadata &out) const {
  // 解析标题
  if (metadata.count("xesam:title")) {
    out.title = metadata.at("xesam:title").get<std::string>();
  } else {
    out.title = "[Unknown Title]"; // 默认值避免空字符串
    DEBUG("Metadata missing xesam:title");
  }

  // 解析艺术家（优先 xesam:artist，降级使用 albumArtist）
  if (metadata.count("xesam:artist")) {
    const auto &artists =
        metadata.at("xesam:artist").get<std::vector<std::string>>();
    out.artist = artists.empty() ? "[Unknown Artist]" : artists[0];
  } else if (metadata.count("xesam:albumArtist")) {
    const auto &albumArtists =
        metadata.at("xesam:albumArtist").get<std::vector<std::string>>();
    out.artist = albumArtists.empty() ? "[Unknown Artist]" : albumArtists[0];
  } else {
    out.artist = "[Unknown Artist]"; // 默认值
    DEBUG("Metadata missing xesam:artist/albumArtist");
  }

  // 解析歌词（musicfox 专有字段）
  if (metadata.count("xesam:asText")) {
    out.lyrics = metadata.at("xesam:asText").get<std::string>();
  } else {
    out.lyrics = ""; // 无歌词时置空
  }
}
void PlayerManager::addNewPlayer(const std::string &serviceName) {
  // 优先使用musicfox播放器
  if (currentPlayer_.find("musicfox") == std::string::npos) {
    currentPlayer_ = serviceName;
  }
  try {
    // 创建播放器实例代理
    sdbus::ServiceName destination{std::move(serviceName)};
    sdbus::ObjectPath objectPath{"/org/mpris/MediaPlayer2"};
    auto playerProxy = sdbus::createProxy(*dbusConn_, std::move(destination),
                                          std::move(objectPath));
    // 注册PropertiesChanged信号监听器
    playerProxy->uponSignal("PropertiesChanged")
        .onInterface("org.freedesktop.DBus.Properties")
        .call([this,
               serviceName](const std::string &interfaceName,
                            std::map<std::string, sdbus::Variant> &changedProps,
                            std::vector<std::string> &invalidatedProps) {
          DEBUG("PropertiesChanged: %s , interfaceName: [%s] , currentPlayer: %s",
                serviceName.c_str(), interfaceName.c_str(), currentPlayer_.c_str());
          if (interfaceName != "org.mpris.MediaPlayer2.Player") {
            WARN("Ignoring non-player interface: %s", interfaceName.c_str());
            return;
          }
          bool needCallback = false;
          if(changedProps.count("Metadata")) {
            needCallback = true;
            auto metadata = changedProps["Metadata"].get<std::map<std::string, sdbus::Variant>>();
            PlayerMetadata md;
            parseMetadata(metadata, md);
            DEBUG("Metadata changed: title=[%s], artist=[%s], lyrics=[%s]",
                  md.title.c_str(), md.artist.c_str(), md.lyrics.c_str());
          }
          if(changedProps.count("PlaybackStatus")) {
            auto status = changedProps["PlaybackStatus"].get<std::string>();
            if( status != "Playing" ) {
              needCallback = true;
            }
            INFO("PlaybackStatus changed: %s", status.c_str());
          }
          // 触发上层回调（线程安全：D-Bus事件循环可能在独立线程，需确保回调线程安全）
          if (stateCallback_ && needCallback) {
            updatePlayerState();
          }
        });

    // 完成信号注册并存储代理
    players_[serviceName] = std::move(playerProxy);
    INFO("New player added: %s", serviceName.c_str());
    if (stateCallback_) {
      updatePlayerState();
    }
  } catch (const sdbus::Error &e) {
    WARN("Player proxy init error: %s", e.what());
  }
}

std::string PlayerManager::getCurrentPlayerName() const {
  // 优先返回musicfox播放器
  return currentPlayer_;
}

std::vector<std::string> PlayerManager::getAllPlayers() const {
  std::vector<std::string> playerNames;
  for (const auto &[name, _] : players_) {
    playerNames.push_back(name);
  }
  return playerNames;
}

// TODO: 实现切换当前播放器的方法（切换显示信息)
void PlayerManager::setCurrentPlayer(const std::string &playerName) {
  currentPlayer_ = playerName;
  updatePlayerState();
}

void PlayerManager::updatePlayerState() {
  DEBUG("updatePlayerState: %s", currentPlayer_.c_str());
  auto state = getPlayerState();
  if (stateCallback_) {
    stateCallback_(state);
  }
}