cmake_minimum_required(VERSION 3.10)
project(gitvis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wxWidgets REQUIRED COMPONENTS core base adv)
include(${wxWidgets_USE_FILE})

add_executable(gitvis
    src/App.cpp
    src/App.h
    src/MainFrame.cpp
    src/MainFrame.h
    src/GitData.h
    src/GitRepository.cpp
    src/GitRepository.h
    src/Layout.cpp
    src/Layout.h
    src/GraphCanvas.cpp
    src/GraphCanvas.h
    src/main.cpp
)

target_link_libraries(gitvis PRIVATE ${wxWidgets_LIBRARIES})
