#include <chrono>
#include <iostream>
#include <sdbus-c++/TypeTraits.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <thread>

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

  // 过滤出以 org.mpris.MediaPlayer2 开头的名称但不包含 playerctld
  std::vector<std::string> filteredNames;
  for (const auto &name : names) {
    if (name.find("org.mpris.MediaPlayer2.") == 0 &&
      name.find("org.mpris.MediaPlayer2.playerctld") == std::string::npos) {
      filteredNames.push_back(name);
    }
  }
  return filteredNames;
}


// Callback function for NameOwnerChanged signal
void onNameOwnerChanged(std::string name, std::string oldOwner,
                        std::string newOwner) {

  std::cout << "NameOwnerChanged signal received:\n"
            << "  Service: " << name << "\n"
            << "  Old owner: " << (oldOwner.empty() ? "none" : oldOwner) << "\n"
            << "  New owner: " << (newOwner.empty() ? "none" : newOwner)
            << "\n";

  if (newOwner.empty()) {
    std::cout << "Music player (" << name << ") has exited.\n";
  } else if (oldOwner.empty()) {
    std::cout << "Music player (" << name << ") has started.\n";
  } else {
    std::cout << "Music player (" << name << ") ownership changed.\n";
  }
}

int main() {
  try {
    std::cout << "Current music players:\n";
    for (const auto &name : listDBusNames()) {
      std::cout << "- " << name << "\n";
    }
    std::cout << "\n--------------------------\n\n";

    // Create a D-Bus connection to the session bus
    auto connection = sdbus::createSessionBusConnection();

    // Create a proxy object for the org.freedesktop.DBus service
    sdbus::ServiceName org_freedesktop_DBus{"org.freedesktop.DBus"};
    sdbus::ObjectPath org_freedesktop_DBus_path{"/org/freedesktop/DBus"};
    auto proxy = sdbus::createProxy(*connection, org_freedesktop_DBus,
                                    org_freedesktop_DBus_path);

    // 注册 NameOwnerChanged 信号处理器
    proxy->uponSignal("NameOwnerChanged")
        .onInterface("org.freedesktop.DBus")
        .call([&](const std::string &name, const std::string &oldOwner,
                  const std::string &newOwner) {
          // 直接传递解析后的参数, 只选择name匹配为 org.mpris.MediaPlayer2.*
          if(name.find("org.mpris.MediaPlayer2.") == 0) {
            onNameOwnerChanged(name, oldOwner, newOwner);
          }
        });

    // 启动事件循环线程
    std::thread eventLoop([&connection]() { connection->enterEventLoop(); });

    // Keep the main thread running
    std::cout << "Monitoring music player D-Bus name changes. Press Ctrl+C to "
                 "exit.\n";
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Clean up (unreachable in this example due to infinite loop)
    eventLoop.join();
  } catch (const sdbus::Error &e) {
    std::cerr << "sdbus error: " << e.what() << "\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Standard error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}