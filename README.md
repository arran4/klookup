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

## Attribution

Data provided by MangaBaka is available under the terms required by MangaBaka and the underlying metadata providers. Third-party data remains subject to those upstream providers' respective terms. For full details on API usage and data licensing, see the [MangaBaka API documentation](https://mangabaka.org/data/api).
## Build Requirements

- CMake 3.16+
- C++17 Compiler
- Qt 6 (Core, Gui, Widgets, Network, DBus, Qml)
- KDE Frameworks 6 (CoreAddons, Config, I18n, Runner, DBusAddons, WidgetsAddons, XmlGui, Wallet, Notifications)

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

## Verifying KRunner activation in an installed KDE session

Install KLookup and its runner plugin, then start a fresh Plasma session. With no
`klookup` process running, run `qdbus6 org.kde.klookup / org.kde.klookup.performSearch "cold start"`
or select a KLookup result in KRunner. KLookup should start, raise its window and
show the query. Repeat with KLookup running and confirm the same process receives
the new tab. If activation fails, enable `QT_LOGGING_RULES="klookup.runner.warning=true"`
for the runner host and inspect the reported D-Bus error. The installed service is
`org.kde.klookup.service`, whose `Name`, object path, interface and method are
`org.kde.klookup`, `/`, `org.kde.klookup`, and `performSearch` respectively.

Provider discovery diagnostics are opt-in: start KLookup with
`QT_LOGGING_RULES="klookup.plugins.debug=true"` to log search paths, candidates,
successful loads and safe loader errors. These diagnostics do not log credentials.
