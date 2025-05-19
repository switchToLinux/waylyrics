#include <iostream>
#include <memory>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <unistd.h> // for getuid()
#include <vector>

// D-Bus 服务和路径常量
const std::string DBUS_SERVICE = "org.freedesktop.DBus";
const std::string DBUS_PATH = "/org/freedesktop/DBus";

std::vector<std::string> listDBusServices() {
  sdbus::Variant result;
  try {
    auto conn = sdbus::createSessionBusConnection();

        // 创建 D-Bus 代理
        sdbus::ServiceName destination{DBUS_SERVICE};
    sdbus::ObjectPath objectPath{DBUS_PATH};
    auto proxy = sdbus::createProxy(*conn, destination, objectPath);

    // 调用 ListNames 方法
    proxy->callMethod("ListNames")
        .onInterface("org.freedesktop.DBus")
        .storeResultsTo(result);

    if (result.isEmpty()) {
      std::cerr << "Error: D-Bus is Empty" << std::endl;
      return {};
    }

  } catch (const sdbus::Error &error) {
    std::cerr << "D-Bus Error: " << error.getName() << " - "
              << error.getMessage() << std::endl;
    return {};
  } catch (const std::exception &ex) {
    std::cerr << "Exception: " << ex.what() << std::endl;
    return {};
  } catch (...) {
    std::cerr << "Unknown error occurred" << std::endl;
    return {};
  }
  // 解析返回值
  std::vector<std::string> services;
  return result.get<std::vector<std::string>>();
}

int main() {
  // 确保以普通用户权限运行
  if (getuid() == 0) {
    std::cerr << "This program should not be run as root!" << std::endl;
    return 1;
  }

  auto services = listDBusServices();
  std::cout << "Found " << services.size()
            << " user-related services:" << std::endl;
  for (const auto &service : services) {
    std::cout << "- " << service << std::endl;
  }
  return 0;
}