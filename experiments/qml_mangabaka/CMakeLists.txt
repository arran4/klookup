# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: BSD-2-Clause

qt_add_library(qml_mangabaka_experiment STATIC
    mangabakaviewmodel.h
    mangabakaviewmodel.cpp
    qmlhostwidget.h
    qmlhostwidget.cpp
)

qt_add_resources(qml_mangabaka_experiment "qml_mangabaka_resources"
    PREFIX "/klookup/experiments/mangabaka"
    FILES
        qml/MangaBakaDetailView.qml
        qml/MangaBakaFullProvider.qml
        fixtures/synthetic_manga.json
)

target_link_libraries(qml_mangabaka_experiment
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickWidgets
)

target_include_directories(qml_mangabaka_experiment
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    add_executable(test_qml_mangabaka_experiment test_qml_mangabaka_experiment.cpp)
    target_link_libraries(test_qml_mangabaka_experiment PRIVATE Qt6::Test Qt6::Quick Qt6::QuickWidgets qml_mangabaka_experiment)
    add_test(NAME test_qml_mangabaka_experiment COMMAND test_qml_mangabaka_experiment)
endif()
