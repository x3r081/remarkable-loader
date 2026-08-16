cmake_minimum_required(VERSION 3.16)
project(rm_launcher VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Gui)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(rm_launcher
    main.cpp
    ../common/penreader.cpp ../common/penreader.h
    ../common/penmouse.cpp ../common/penmouse.h)
target_include_directories(rm_launcher PRIVATE ../common)

qt_add_qml_module(rm_launcher
    URI rm_launcher_module
    VERSION 1.0
    QML_FILES Main.qml)

target_link_libraries(rm_launcher PRIVATE Qt6::Quick Qt6::GuiPrivate)
