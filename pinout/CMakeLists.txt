# LiteTools/pinout —— 开发板引脚定义图制作工具（C++/Qt6 Widgets，无 QML）
# 构建：
#   cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=<MSYS2 ucrt64 前缀>
#   cmake --build build
#   ctest --test-dir build
cmake_minimum_required(VERSION 3.22)
project(pinout VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Widgets
    Gui
    Test
)
qt_standard_project_setup()

qt_add_executable(pinout
    src/main.cpp
    src/project.h
    src/project.cpp
    src/layout.h
    src/layout.cpp
    src/exporter.h
    src/exporter.cpp
    src/startpage.h
    src/startpage.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/boardview.h
    src/boardview.cpp
    src/boardscene.h
    src/boardscene.cpp
    src/pinitem.h
    src/pinitem.cpp
    src/propspanel.h
    src/propspanel.cpp
    src/librarydlg.h
    src/librarydlg.cpp
)
target_include_directories(pinout PRIVATE
    src
)
target_link_libraries(pinout PRIVATE
    Qt6::Widgets
)

enable_testing()

# 模型/布局/导出的离屏单测（QT_QPA_PLATFORM=offscreen，见 set_tests_properties）
qt_add_executable(test_project
    tests/test_project.cpp
    src/project.h
    src/project.cpp
)
target_include_directories(test_project PRIVATE
    src
)
target_link_libraries(test_project PRIVATE
    Qt6::Gui
    Qt6::Test
)

qt_add_executable(test_layout
    tests/test_layout.cpp
    src/layout.h
    src/layout.cpp
    src/project.h
    src/project.cpp
)
target_include_directories(test_layout PRIVATE
    src
)
target_link_libraries(test_layout PRIVATE
    Qt6::Gui
    Qt6::Test
)

qt_add_executable(test_exporter
    tests/test_exporter.cpp
    src/exporter.h
    src/exporter.cpp
    src/layout.h
    src/layout.cpp
    src/project.h
    src/project.cpp
)
target_include_directories(test_exporter PRIVATE
    src
)
target_link_libraries(test_exporter PRIVATE
    Qt6::Gui
    Qt6::Test
)

add_test(NAME
    project
    COMMAND
    test_project
)
add_test(NAME
    layout
    COMMAND
    test_layout
)
add_test(NAME
    exporter
    COMMAND
    test_exporter
)
set_tests_properties(project layout exporter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
