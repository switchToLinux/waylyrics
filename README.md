# waylyrics
> a lyrics plugin for waybar


## Build

```bash
meson setup build
meson compile -C build

```


## waybar 使用

使用方法：
- 鼠标左键点击: 切换歌词显示功能开关(关闭后不会显示歌词，但会显示正在播放的音乐名称)
- 鼠标右键点击: 播放器暂停/播放
- 鼠标中键点击: 切换下一个可用的播放器
- 鼠标滚轮滚动向上: 上一首音乐
- 鼠标滚轮滚动向下: 下一首音乐



```json
{
    "cffi/lyrics": {
        "module_path": "/apps/libs/waylyrics.so",
        "cache_dir": "~/.cache/waylyrics",
        "id": "waylyrics",
        "class": "lyrics-mpv",
        "interval": 3,
        "dest": "mpv"
    },
}
```

参数说明:
- module_path: 插件路径
- id: css样式id ,默认值为 cffi-lyrics-label
- class: css样式class，默认值为 cffi-lyrics-label
- interval: 歌词刷新时间间隔，单位秒，默认为 3
- dest: 播放器实例名称,暂时没有实现此功能, mpris表示所有支持mpris协议的播放器，应用于dbus的 **org.mpris.MediaPlayer2.{dest}**，比如 mpv, vlc, mpris 等.
- cache_dir: 歌词缓存目录, 用于缓存歌词, 避免每次都请求歌词, 默认为 ~/.cache/waylyrics
-

## dbus命令帮助

```bash

# 监视当前播放器的属性变化
dbus-monitor --session interface=org.freedesktop.DBus.Properties path=/org/mpris/MediaPlayer2/Player member=PropertiesChanged

# 查看 PropertiesChanged 事件
dbus-monitor --session interface=org.freedesktop.DBus.Properties path=/org/mpris/MediaPlayer2/Player member=PropertiesChanged


# 查看当前播放器的播放位置
dbus-send --session --dest=org.mpris.MediaPlayer2.musicfox.instance176348 --print-reply --type=method_call /org/mpris/MediaPlayer2/Player org.freedesktop.DBus.Properties.Get string:"org.mpris.MediaPlayer2.Player" string:"Position"


```

## 功能逻辑说明

waylyrics是一个CFFI动态库，基于sdbus-cpp v2.1.0版本开发实现（代码中通过 `sdbus::createProxy` 创建D-Bus代理）。

当将waylyrics配置到waybar后，会在状态栏中显示歌词。默认情况下，会每隔3秒刷新一次歌词（对应配置中的 `interval` 参数）。

### 初始化阶段
歌词显示功能开关控制：鼠标左键点击标签切换歌词显示功能开关，默认情况下为关闭状态。

### 检测有效的播放器
当检测到支持mpris协议的播放器时，优先判断逻辑如下（代码实现见 `getPlayingName` 函数）：
- 优先考虑状态为 **Playing** 的播放器（通过D-Bus获取 `PlaybackStatus` 属性判断）
- 当前状态为 **Playing** 的播放器存在多个时，按以下优先级选择：  
  `musicfox > mpv > vlc > 其他播放器（firefox, chrome, edge等）`（代码中通过遍历播放器名称列表实现优先级判断）

### 歌词获取
获取歌词的逻辑如下（核心实现见 `getLyrics` 函数）：
- 如果当前播放器为 **musicfox**，则通过D-Bus获取歌词（需通过 `org.mpris.MediaPlayer2.Player` 接口的 `Metadata` 属性获取），并缓存到本地（缓存路径为 `~/.cache/waylyrics` 下的哈希文件名）。
- 如果当前播放器为其他播放器时：
  - 优先检测本地缓存是否存在歌词（通过 `std::filesystem::exists` 检查缓存文件），存在则直接读取缓存；
  - 缓存不存在时，通过 `https://lrclib.net/api/search` 接口获取歌词（代码中使用libcurl发起HTTP请求），并异步写入本地缓存（通过分离线程实现缓存写入）。

### 播放器状态改变监测（D-Bus事件处理）
需要检测D-Bus上的播放器事件（代码中通过 `getNowPlaying` 函数轮询 `PlaybackStatus` 属性）：
- 当前播放器断开时，会触发重新检测有效播放器（通过异常捕获实现连接状态判断）；
- 监测到当前代理的播放器状态从 **Playing** 变为其他状态（如Paused/Stoped）时，会触发重新检测播放器（通过 `getNowPlaying` 返回状态码触发上层逻辑）。


## 获取歌词方法


### musicfox后台获取歌词

通过dbus获取歌词, 可以通过以下命令获取歌词, 其中 **org.mpris.MediaPlayer2.musicfox.instance176348** 为实例名称, 可以通过 **dbus-monitor** 命令获取, 也可以通过 **dbus-send** 命令获取.

```bash
dbus-send --print-reply --session \ 
  --dest=org.mpris.MediaPlayer2.musicfox.instance176348 \
  /org/mpris/MediaPlayer2 \
  org.freedesktop.DBus.Properties.Get \
  string:"org.mpris.MediaPlayer2.Player" \
  string:"Metadata"
```
采用sdbus-cpp开发实现代码：
```cpp
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>

int main() {
    try {
        auto connection = sdbus::createSessionBusConnection();
        auto proxy = connection->createObjectProxy("org.mpris.MediaPlayer2.musicfox.instance176348", "/org/mpris/MediaPlayer2");
        auto properties = proxy->createInterface("org.freedesktop.DBus.Properties");
        auto metadata = properties->callMethod("Get", "org.mpris.MediaPlayer2.Player", "Metadata");
        std::cout << metadata << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}

```


### 163获取歌词

```bash
$ curl "https://music.163.com/api/song/lyric?id=188451&lv=1&kv=1&tv=-1" 

{"sgc":false,"sfy":false,"qfy":false,"lyricUser":{"id":614806,"status":99,"demand":0,"userid":247658846,"nickname":"钊业","uptime":1366946758186},"lrc":{"version":34,"lyric":"[00:00.000] 
作词 : 黄霑\n[00:01.000] 作曲 : 顾嘉辉\n[00:25.700]轻轻笑声 在为我送温暖\n[00:31.090]你为我注
入快乐强电\n[00:37.900]轻轻说声 漫长路快要走过\n[00:43.900]终于走到明媚晴天\n[00:49.100]声声欢呼跃起 像红日发放金箭\n[00:56.400]我伴你往日笑面重现\n[01:02.900]轻轻叫声 共抬望眼看高空\n[01:08.900]终于青天优美为你献\n[01:15.700]拥着你 当初温馨再涌现\n[01:21.900]心里边 童年稚气梦未污
染\n[01:28.000]今日我 与你又试肩并肩\n[01:34.800]当年情 此刻是添上新鲜\n[01:47.700]一望你 眼里温馨已通电\n[01:53.900]心里边 从前梦一点未改变\n[01:59.900]今日我 与你又试肩并肩\n[02:06.480]
当年情 再度添上新鲜\n[02:21.100]如果说\n[02:23.900]拍吴宇森的电影是一种享受的话\n[02:26.000]我同意\n[02:27.900]如果说和周润发拍电影更加是一种享受的话\n[02:31.900]我更加同意\n[02:34.870]理
由就是\n[02:35.900]他真是一位超级偶像兼一位超级演员\n[02:39.900]再给点掌声(送给)我心爱的发哥\n[02:45.900]欢呼跃起 像红日发放金箭\n[02:51.700]我伴你往日笑面重现\n[02:58.100]轻轻叫声 共抬望
眼看高空\n[03:04.200]终于青天优美为你献\n[03:10.950]拥着你 当初温馨再涌现\n[03:17.090]心里边 
童年稚气梦未污染\n[03:23.900]今日我 与你又试肩并肩\n[03:29.900]当年情 此刻是添上新鲜\n[03:43.090]一望你 眼里温馨已通电\n[03:49.900]心里边 从前梦一点未改变\n[03:55.900]今日我 与你又试肩并肩\n[04:02.100]当年情 再度添上新鲜\n[04:29.900]多谢\n[04:30.700]多谢你们\n[04:31.900]很开心你们
这么喜欢《当年情》这首歌\n[04:35.900]我记得我去年夏天的时候出了一张唱片\n[04:38.900]整张唱片都是我翻唱别人的歌\n[04:42.890]为什么我会有这样的行动呢？\n[04:43.900]因为我觉得我真的很喜欢里面的那十首歌\n[04:47.890]另一个原因就是我觉得香港的乐坛\n[04:49.900]其实有很多精英分子\n[04:51.900]他们对乐坛付出了很多力量\n[04:54.100]所以在这里 我希望借现在这个小小的机会\n[04:57.900]唱下面几首歌 献给大家\n[05:00.900]由我为乐坛人士致敬的SALUTE MEDLEY！\n"},"klyric":{"version":0,"lyric":""},"tlyric":{"version":0,"lyric":""},"code":200}
```

