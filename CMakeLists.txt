cmake_minimum_required(VERSION 3.16)

project(novel_downloader VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动处理 Qt 的 moc / uic / rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)

qt_add_executable(novel_downloader
    main.cpp
    mainwindow.cpp
    mainwindow.h
    biquga.cpp
    biquga.h
)

target_link_libraries(novel_downloader PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
)

# Windows 下作为 GUI 程序（不弹黑框）
set_target_properties(novel_downloader PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Android 打包支持
if(ANDROID)
    set_target_properties(novel_downloader PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()

install(TARGETS novel_downloader
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
