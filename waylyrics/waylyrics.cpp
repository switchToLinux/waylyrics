
#include <cstdint>
#include <cstring>
#include <gtk/gtk.h>
#include "lib.hpp"
#include "waybar_cffi_module.h"
#include <iterator>
#include <string>
#include <thread>

typedef struct {
  wbcffi_module *waybar_module;
  GtkBox *container;
} Mod;

static int instance_count = 0;
const size_t wbcffi_version = 1;
// 默认css类名 "cffi-lyrics-mpv"
const char *defaultCssClass = "cffi-lyrics-label";
const char *defaultLabelId = "cffi-lyrics-label"; // 默认ID
const char *defaultDestName = "mpv";
// 默认更新间隔时间（秒）
const int64_t defaultUpdateInterval = 3;

void *wbcffi_init(const wbcffi_init_info *init_info,
                  const wbcffi_config_entry *config_entries,
                  size_t config_entries_len) {
  fprintf(stderr, "waylyrics: init config, entries len:%ld\n", config_entries_len);
  std::string cssClass = defaultCssClass;
  std::string labelId = defaultLabelId;
  std::string destName = defaultDestName;
  int64_t updateInterval = defaultUpdateInterval;
  std::string cacheDir;
  for (size_t i = 0; i < config_entries_len; i++) {
    fprintf(stderr, "  %s = %s\n", config_entries[i].key,
            config_entries[i].value);
    if (strncmp(config_entries[i].key, "class", std::size("class")) == 0) {
      cssClass = config_entries[i].value;
    } else if (strncmp(config_entries[i].key, "id", std::size("id")) == 0) {
      labelId = config_entries[i].value;
    } else if (strncmp(config_entries[i].key, "dest", std::size("dest")) == 0) {
      destName = config_entries[i].value;
    } else if (strncmp(config_entries[i].key, "interval",
                        std::size("interval")) == 0) {
      updateInterval = atoi(config_entries[i].value);
    } else if (strncmp(config_entries[i].key, "cache_dir", std::size("cache_dir")) == 0) {
      cacheDir = config_entries[i].value;
    }
    else if (strncmp(config_entries[i].key, "module_path",
                       std::size("module_path"))) {
      fprintf(stderr, "ignore config key: %s\n", config_entries[i].key);
    } else {
      fprintf(stderr, "Unknown config key: %s\n", config_entries[i].key);
    }
  }
  if (destName.empty()) {
    fprintf(stderr, "destName is empty, use default: %s\n", defaultDestName);
    destName = defaultDestName;
  }

  Mod *inst = (Mod *)malloc(sizeof(Mod));
  inst->waybar_module = init_info->obj;

  GtkContainer *root = init_info->get_root_widget(init_info->obj);

  inst->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 5));
  gtk_container_add(GTK_CONTAINER(root), GTK_WIDGET(inst->container));

  init(destName, cacheDir);

  GtkLabel *label = GTK_LABEL(gtk_label_new(loadingText));
  GtkStyleContext *label_context = gtk_widget_get_style_context(GTK_WIDGET(label));
  
  gtk_style_context_add_class(label_context, cssClass.c_str());
  gtk_widget_set_name(GTK_WIDGET(label), labelId.c_str());
  
  gtk_container_add(GTK_CONTAINER(inst->container), GTK_WIDGET(label));

  std::thread([label, updateInterval]() {
    for (;;) {
      auto d = getCurrentLine();
      if (d.has_value()) {
        auto [line, pos, dur] = d.value();
        gtk_label_set_text(label, line.c_str());
      }
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(std::chrono::seconds(updateInterval));
    }
  }).detach();

  fprintf(stderr, "waylyrics inst=%p: init success ! (%d total instances)\n",
          inst, ++instance_count);
  return inst;
}

void wbcffi_deinit(void *instance) {
  fprintf(stderr, "waylyrics inst=%p: free memory\n", instance);
  free(instance);
}

// void wbcffi_update(void *instance) {
//   fprintf(stderr, "waylyrics inst=%p: Update request\n", instance);
// }
//
// void wbcffi_refresh(void *instance, int signal) {
//   fprintf(stderr, "waylyrics inst=%p: Received refresh signal %d\n",
//   instance,
//           signal);
// }
//
// void wbcffi_doaction(void *instance, const char *name) {
//   fprintf(stderr, "waylyrics inst=%p: doAction(%s)\n", instance, name);
// }
