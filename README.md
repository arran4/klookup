# KLookup

**KLookup** is a dictionary-style multi-provider search tool natively built for KDE Plasma environments utilizing Qt6 and KDE Frameworks 6 (KF6).

It allows you to look up queries across various information providers globally, all from a single window. The application embraces KDE's standard desktop integration logic via D-Bus, KConfig, KNotification, KWallet, and more, providing a robust, fast, and secure tool for knowledge workers.

## Key Features

- **Multi-Provider Architecture**: Query data from Wikipedia, GitHub, MangaUpdates, AniList, TVMaze, LyricsOVH, DuckDuckGo, OpenLibrary, TMDb, and OMDb.
- **Dynamic Tab Interface**: View multiple active queries across non-blocking tabs.
- **KRunner Integration**: Includes a companion D-Bus plugin that integrates natively into KRunner (`Alt` + `Space` > `:q: query`).
- **Clipboard Monitoring**: Automatically monitor or auto-search queries derived straight from clipboard activity.
- **Single Instance Control**: Operates as a single unique instance per session via `KDBusService`.
- **Plugin System**: Every backend is an independent, dynamic `QPluginLoader` module, making it extraordinarily easy to add custom third-party integrations.
- **KWallet Integration**: Secure authentication capabilities natively backed by KDE's local wallet storage subsystem.

## Build Requirements

- CMake 3.16+
- C++17 Compiler
- Qt 6 (Core, Gui, Widgets, Network, DBus)
- KDE Frameworks 6 (CoreAddons, Config, I18n, Runner, DBusAddons, WidgetsAddons, XmlGui, Wallet)

## Building

To build the KLookup application along with all default backend plugins:

```bash
mkdir build && cd build
cmake ..
cmake --build . --target klookup_with_plugins
```

## Running

Launch the main binary to invoke the dashboard:

```bash
./src/klookup
```

Alternatively, invoke a quick search directly from the command line:

```bash
./src/klookup "Linux kernel"
```
