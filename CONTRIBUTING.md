<!--
SPDX-FileCopyrightText: 2026 KLookup contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Contributing to KLookup

Thank you for your interest in contributing to KLookup! KLookup is an extensible dictionary-style multi-provider lookup desktop application built with Qt 6 and KDE Frameworks 6.

## Development Environment

KLookup is built with standard modern C++ (C++17), CMake, Qt 6, and KDE Frameworks 6 (KF6).

### 1. Universal Project Requirements

To build KLookup, your environment requires:
- C++17 compatible compiler (GCC or Clang)
- CMake >= 3.16 and Ninja (or Make)
- Qt 6 (Core, Gui, Widgets, Network, DBus, Qml; Test when BUILD_TESTING is enabled)
- KDE Frameworks 6 (CoreAddons, Config, I18n, WidgetsAddons, XmlGui, Notifications, Wallet, GlobalAccel, Runner, DBusAddons)

### 2. Supported Workflows

Developers can build KLookup either:
1. **Natively** on distributions providing Qt 6 and KF6 development packages (e.g., Debian testing/unstable, Arch Linux, Fedora, openSUSE).
2. **Via the provided rootfs helper scripts** (`.jules/bootstrap.sh` and `.jules/run.sh`), which provision a reproducible Debian trixie/forky rootfs with all required dependencies.

> [!NOTE]
> **Jules-managed environments:** When running in a Jules-managed environment, use the repository's `.jules` runners and follow that environment's container restrictions. Outside Jules, developers may build natively or use another compatible container environment. The project's Qt/KF6/CMake requirements are independent of the agent or runner.

> [!NOTE]
> **Continuous Integration (CI):** GitHub Actions uses `.jules/bootstrap.sh` and `.jules/run.sh` to run builds and test suites against the shared development rootfs.

### 3. Rootfs-based Development Workflow

If using the chrooted development rootfs:

#### Step 1: Provision the Environment

Bootstrap the shared KDE 6 rootfs:

```bash
sudo apt-get update && sudo apt-get install -y zstd
./.jules/bootstrap.sh
```

#### Step 2: Install Required Build Dependencies

Inside the rootfs, install the required KDE development headers:

```bash
.jules/run.sh apt-get update
.jules/run.sh apt-get install -y libkf6runner-dev libkf6globalaccel-dev cmake ninja-build build-essential g++
```

#### Step 3: Build and Test

Configure and compile the project using CMake and Ninja (prefix with `.jules/run.sh` when using the rootfs, or run directly for native builds):

```bash
# Configure with tests enabled
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON

# Compile in parallel
cmake --build build --parallel

# Run all test suites
ctest --test-dir build --output-on-failure
```

#### Step 4: Sanitizer Auditing (ASan & UBSan)

We maintain strict memory and type safety. Before submitting significant changes, verify your code under sanitizers:

```bash
.jules/run.sh cmake -S . -B build-san -G Ninja -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"

.jules/run.sh cmake --build build-san --parallel
.jules/run.sh ctest --test-dir build-san --output-on-failure
```

## Architecture & Provider Plugins

KLookup is organized into:
- `src/`: Core application, main window, search tabs, dashboard, settings, clipboard policy, and D-Bus interfaces.
- `providers/`: Modular search providers built as standalone Qt plugins (`.so`).
- `runner/`: Plasma KRunner integration plugin (`krunner_klookup`).
- `tests/`: Automated unit and integration tests covering providers, network flows, UI controls, and lifecycle.

### Writing a New Provider Plugin

To add a new search provider:
1. Create a subdirectory under `providers/<provider_id>/`.
2. Inherit from `SearchProvider` (and optionally `SearchProviderContinuation` for pagination and deferred content loading).
3. Export the plugin using `Q_PLUGIN_METADATA(IID SearchProvider_iid FILE "provider_<provider_id>.json")`.
4. Provide unit tests under `tests/` with offline fixture data (e.g. mock JSON/HTML replies) to prevent reliance on external network stability during test runs.

## Code Standards & Licensing

- **C++ Standards**: Use modern C++17 with Qt 6 idioms.
- **Formatting**: Preserve existing formatting and avoid unrelated whitespace churn (`git diff --check`).
- **Documentation**:
  - Code and unit tests are authoritative for current runtime behavior.
  - GitHub issues and pull requests serve as permanent historical records of requirements and design decisions.
  - Source comments should preserve non-obvious local reasoning, security invariants, or upstream API quirks.
  - Avoid committing temporary agent manifest or tracking files to the repository.
- **REUSE 3.3 Compliance**:
  - Every file must include SPDX copyright and license tags (or be mapped in `REUSE.toml`).
  - Source code uses `GPL-3.0-or-later`.
  - Build scripts and CMake definitions use `BSD-2-Clause`.
  - Documentation and text guides use `CC-BY-SA-4.0`.
  - Icons and design assets use `CC0-1.0`.
  - Run `.jules/run.sh reuse lint` to verify compliance before committing.

## Submitting Pull Requests

1. Fork the repository and create a feature branch from `main`.
2. Keep pull requests focused, atomic, and scoped to a single feature or fix.
3. Ensure all automated tests pass (`ctest --output-on-failure`).
4. Write a descriptive PR title and summary explaining the motivation, architectural changes, and testing strategy. Reference any relevant GitHub issues (`Fixes #...`).
