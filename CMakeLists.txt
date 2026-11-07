cmake_minimum_required(VERSION 3.20)
project(minimum_times LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(pugixml REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GUMBO REQUIRED IMPORTED_TARGET gumbo)
find_package(Threads REQUIRED)

# Everything except main.cpp is a library so the tests can use it too.
add_library(mt_core STATIC
  src/util.cpp
  src/http.cpp
  src/rss.cpp
  src/gnews.cpp
  src/extract.cpp
  src/cluster.cpp
  src/score.cpp
  src/summarize.cpp
  src/render.cpp
  src/pipeline.cpp
)
target_include_directories(mt_core PUBLIC src)
target_link_libraries(mt_core PUBLIC
  CURL::libcurl nlohmann_json::nlohmann_json pugixml::pugixml PkgConfig::GUMBO Threads::Threads)
if(MSVC)
  target_compile_options(mt_core PRIVATE /W4)
else()
  target_compile_options(mt_core PRIVATE -Wall -Wextra)
endif()

add_executable(minimum_times src/main.cpp)
target_link_libraries(minimum_times PRIVATE mt_core)

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE mt_core)
add_test(NAME core COMMAND test_core)
