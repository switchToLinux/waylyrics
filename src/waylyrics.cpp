#include "../include/way_lyrics.h"
#include "../include/waybar_cffi_module.h"
#include "common.h"
#include <gtk/gtk.h>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>

const size_t wbcffi_version = 1;

// 默认配置参数
constexpr const char *defaultCssClass = "waylyrics-label";
constexpr const char *defaultLabelId = "waylyrics-label";
constexpr const char *defaultDestName = "org.mpris.MediaPlayer2.musicfox";
constexpr int defaultUpdateInterval = 1; // 秒
constexpr const char *loadingText = "加载歌词...";

// 插件实例结构体（管理生命周期）
struct Mod {
  void *waybar_module;                  // waybar模块对象
  GtkBox *container;                    // GTK容器
  std::unique_ptr<WayLyrics> wayLyrics; // 歌词显示管理器
};

// 全局实例计数（用于调试）
static int instance_count = 0;

// 配置解析辅助函数（从waybar配置中提取参数）
static std::tuple<std::string, std::string, std::string, int, std::string>
parseConfig(const wbcffi_config_entry *config_entries,
            size_t config_entries_len) {
  std::string cssClass = defaultCssClass;
  std::string labelId = defaultLabelId;
  std::string destName = defaultDestName;
  int updateInterval = defaultUpdateInterval;
  std::string cacheDir = std::string(getenv("HOME")) + "/.cache/waylyrics";
  for (size_t i = 0; i < config_entries_len; ++i) {
    const auto &entry = config_entries[i];
    if (strncmp(entry.key, "class", 5) == 0) {
      cssClass = entry.value;
    } else if (strncmp(entry.key, "id", 2) == 0) {
      labelId = entry.value;
    } else if (strncmp(entry.key, "dest", 4) == 0) {
      destName = entry.value;
    } else if (strncmp(entry.key, "interval", 8) == 0) {
      updateInterval = std::max(1, atoi(entry.value)); // 最小间隔1秒
    } else if (strncmp(entry.key, "cache_dir", 10) == 0) {
      cacheDir = entry.value;
    } else {
      DEBUG("waylyrics: 未知配置项 '%s'", entry.key);
    }
  }
  if (cssClass.empty()) {
    cssClass = defaultCssClass;
  }
  if (labelId.empty()) {
    labelId = defaultLabelId;
  }
  if (destName.empty()) {
    destName = defaultDestName;
  }
  if (updateInterval <= 0) {
    updateInterval = defaultUpdateInterval;
  }
  if (cacheDir.empty()) {
    cacheDir = std::string(getenv("HOME")) + "/.cache/waylyrics";
  }
  DEBUG("waylyrics: 配置解析完成，参数: class=%s, id=%s, dest=%s, interval=%d, cache_dir=%s",
        cssClass.c_str(), labelId.c_str(), destName.c_str(), updateInterval, 
        cacheDir.c_str());
  return {cssClass, labelId, destName, updateInterval, cacheDir};
}

// waybar插件初始化入口（waybar要求的固定接口）
void *wbcffi_init(const wbcffi_init_info *init_info,
                  const wbcffi_config_entry *config_entries,
                  size_t config_entries_len) {
  INFO("waylyrics: 初始化插件，配置项数量: %ld", config_entries_len);

  // 解析配置参数
  auto [cssClass, labelId, destName, updateInterval, cacheDir] =
      parseConfig(config_entries, config_entries_len);

  // 创建插件实例结构体
  Mod *inst = (Mod *)malloc(sizeof(Mod));
  inst->waybar_module = init_info->obj;

  // 创建GTK容器和标签
  GtkContainer *root = init_info->get_root_widget(init_info->obj);
  inst->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 5));
  gtk_container_add(GTK_CONTAINER(root), GTK_WIDGET(inst->container));

  GtkLabel *label = GTK_LABEL(gtk_label_new(loadingText));
  GtkStyleContext *label_context =
      gtk_widget_get_style_context(GTK_WIDGET(label));
  gtk_style_context_add_class(label_context, cssClass.c_str()); // 应用CSS类
  gtk_widget_set_name(GTK_WIDGET(label), labelId.c_str());      // 设置标签ID
  gtk_container_add(GTK_CONTAINER(inst->container), GTK_WIDGET(label));

  // 初始化WayLyrics实例（传递配置参数）
  inst->wayLyrics =
      std::make_unique<WayLyrics>(cacheDir, updateInterval, cssClass);
  inst->wayLyrics->start(label); // 启动歌词显示

  INFO("waylyrics: 实例 %p 初始化完成（总实例数: %d）", inst, ++instance_count);
  return inst;
}

// waybar插件销毁接口（可选，根据需要实现）
void wbcffi_finish(void *data) {
  if (!data)
    return;
  Mod *inst = (Mod *)data;
  if (inst->wayLyrics) {
    inst->wayLyrics->stop(); // 停止歌词刷新
  }
  gtk_widget_destroy(GTK_WIDGET(inst->container)); // 销毁GTK部件
  delete inst;
  INFO("waylyrics: 实例销毁完成（剩余实例数: %d）", --instance_count);
}

