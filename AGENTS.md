# Agent Instructions

- **Do not use Docker for the development/test environment.**
- `.jules/bootstrap.sh` provisions the shared KDE development rootfs from `ghcr.io/arran4/kde-dev-rootfs:forky-latest`.
- All build and test commands that require KDE/Qt dependencies must run through `.jules/run.sh`.
- The repository uses CMake to build. To build and test, you should run:
  ```bash
  .jules/run.sh apt-get update
  .jules/run.sh apt-get install -y libkf6runner-dev
  .jules/run.sh cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
  .jules/run.sh cmake --build build --parallel
  .jules/run.sh ctest --test-dir build --output-on-failure
  ```
- Make sure to call `sudo apt-get install -y libkf6runner-dev` through `.jules/run.sh` first to install the KF6Runner component.
- **New standalone tests:** add `tests/registrations/test_<name>.cmake` with a unique matching executable/test name, not a new block in `tests/CMakeLists.txt`. The parent file includes registration fragments in sorted order with `CONFIGURE_DEPENDS`. Keep any helper targets and ordering constraints in the same fragment. Run `python3 tests/check_cmake_registrations.py --self-test` and `python3 tests/check_cmake_registrations.py`; CI enforces duplicate detection. Do not sort `target_link_libraries` indiscriminately (link order can matter).
