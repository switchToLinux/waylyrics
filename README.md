# waylyrics
> a lyrics plugin for waybar


## Build

```bash
meson setup build
meson compile -C build

```


## waybar 使用

```json
{
    "cffi/lyrics": {
        "module_path": "/apps/libs/waylyrics.so",
        "id": "waylyrics",
        "class": "lyrics-mpv",
        "filter": "mpv"
    },
}
```

参数说明:
- module_path: 插件路径
- id: css样式id
- class: css样式class
- filter: 过滤播放器类型，比如 mpv, vlc 等

