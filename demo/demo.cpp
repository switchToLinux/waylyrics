#include <iostream>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/TypeTraits.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <vector>

// 播放器状态变化的回调函数类型
using PlayerChangeCallback =
    std::function<void(const std::string &playerName, bool added)>;

std::vector<std::string> listDBusNames() {
  // 连接到 session bus
  auto connection = sdbus::createSessionBusConnection();

  // 创建 org.freedesktop.DBus 的代理对象
  sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
  sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
  auto proxy = sdbus::createProxy(*connection, org_freedesktop_DBus,
                                  org_freedesktop_DBus_path);

  // 调用 ListNames 方法
  std::vector<std::string> names;
  proxy->callMethod("ListNames")
      .onInterface("org.freedesktop.DBus")
      .storeResultsTo(names);

  return names;
}

void onConcatenated(sdbus::Signal signal) {
  std::string concatenatedString;
  signal >> concatenatedString;

  std::cout << "Received signal with concatenated string " << concatenatedString
            << std::endl;
}

// 播放器管理器类
class MprisPlayerManager {
public:
  MprisPlayerManager() {
    // 初始化时获取现有播放器
    refreshPlayers();

    try {
      // 创建D-Bus连接
      auto connection = sdbus::createSessionBusConnection();

      // 创建用于监听信号的代理
      sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
      sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
      m_dbusSignalProxy = sdbus::createProxy(*connection, org_freedesktop_DBus,
                                             org_freedesktop_DBus_path);

      // 注册NameOwnerChanged信号监听
      sdbus::InterfaceName org_freedesktop_DBus_interface{"org.freedesktop.DBus"};
      sdbus::SignalName org_freedesktop_DBus_signal{"NameOwnerChanged"};

      m_dbusSignalProxy
          ->registerSignalHandler(org_freedesktop_DBus_interface,
                                  org_freedesktop_DBus_signal, &onConcatenated);
      // 完成信号注册
      // m_dbusSignalProxy->finishRegistration();
    } catch (const sdbus::Error &e) {
      std::cerr << "Error setting up D-Bus signal listener: " << e.what()
                << std::endl;
    }
  }
  const std::vector<std::string> &getPlayers() const { return m_players; }

  // 虚函数，可被子类重写
  virtual void onPlayerAdded(const std::string &playerName) {
    std::cout << "新播放器: " << playerName << std::endl;
  }

  virtual void onPlayerRemoved(const std::string &playerName) {
    std::cout << "播放器已移除: " << playerName << std::endl;
  }

private:
  std::vector<std::string> m_players;
  std::unique_ptr<sdbus::IProxy> m_dbusSignalProxy; // 新增：持有信号监听代理

  void refreshPlayers() { m_players = getMprisPlayerNames(); }

  // 前面定义的 getMprisPlayerNames 函数
  std::vector<std::string> getMprisPlayerNames() {
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

        
      // 调用 ListNames 方法
      std::vector<std::string> allNames;
      dbusProxy->callMethod("ListNames")
          .onInterface("org.freedesktop.DBus")
          .storeResultsTo(allNames);

      // 解析结果
      for (const auto &name : allNames) {
        if (name.find("org.mpris.MediaPlayer2.") == 0) {
          playerNames.push_back(name);
        }
      }
    } catch (const sdbus::Error &e) {
      std::cerr << "Error getting player names: " << e.what() << std::endl;
    }
    return playerNames;
  }
};

// 示例用法
int main() {
  // 创建播放器管理器
  MprisPlayerManager playerManager;

  // 打印当前所有播放器
  std::cout << "当前活动的播放器:" << std::endl;
  for (const auto &player : playerManager.getPlayers()) {
    std::cout << "- " << player << std::endl;
  }

  // 保持程序运行以监听变化
  std::cout << "按 Ctrl+C 退出..." << std::endl;
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}
