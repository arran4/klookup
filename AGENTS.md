# Agent Instructions

- **Do not use Docker for the development/test environment.**
- `.jules/bootstrap.sh` provisions the shared KDE development rootfs from `ghcr.io/arran4/kde-dev-rootfs:forky-latest`.
- All build and test commands that require KDE/Qt dependencies must run through `.jules/run.sh`.
- The repository uses CMake to build. To build and test, you should run:
  ```bash
  .jules/run.sh sudo apt-get update
  .jules/run.sh sudo apt-get install -y libkf6runner-dev
  .jules/run.sh cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
  .jules/run.sh cmake --build build --parallel
  .jules/run.sh ctest --test-dir build --output-on-failure
  ```
- Make sure to call `sudo apt-get install -y libkf6runner-dev` through `.jules/run.sh` first to install the KF6Runner component.
