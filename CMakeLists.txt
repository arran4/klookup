cmake_minimum_required(VERSION 3.16)
project(klookup VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(QT_MIN_VERSION "6.0.0")

# Mocking find_package for verification because of environment limitations
# find_package(ECM REQUIRED NO_MODULE)
# set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
# include(KDEInstallDirs)
# include(KDECMakeSettings)
# include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    DBus
)

add_subdirectory(src)
add_subdirectory(providers/wikipedia)
add_subdirectory(providers/github)
add_subdirectory(providers/mangaupdates)
add_subdirectory(providers/anilist)
add_subdirectory(providers/tvmaze)
add_subdirectory(providers/lyrics_ovh)
add_subdirectory(providers/duckduckgo)
add_subdirectory(providers/openlibrary)
add_subdirectory(providers/themoviedb)
add_subdirectory(providers/omdb)
add_subdirectory(providers/myanimelist)
add_subdirectory(providers/mangabaka)
add_subdirectory(runner)
