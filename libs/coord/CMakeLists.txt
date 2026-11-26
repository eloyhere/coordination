add_library(coorc_core STATIC
  src/support.cpp
  src/platform.cpp
  src/toml.cpp
  src/archive.cpp
  src/manifest.cpp
  src/lockfile.cpp
  src/net.cpp
  src/mirror.cpp
  src/registry.cpp
  src/resolver.cpp
  src/package_manager.cpp
  src/lexer.cpp
  src/ast_dump.cpp
  src/parser.cpp
  src/sema.cpp
  src/interp.cpp
  src/generate.cpp
  src/driver.cpp
  src/standard_library.cpp
)

target_include_directories(coorc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(coorc_core PUBLIC cxx_std_20)

# std::thread is used by the cross-platform process runner. Threads::Threads is
# empty on Windows and adds -pthread where the platform needs it.
find_package(Threads REQUIRED)
target_link_libraries(coorc_core PUBLIC Threads::Threads)
target_compile_definitions(coorc_core PRIVATE
  COORD_STDLIB_DIR="${CMAKE_SOURCE_DIR}/lib/standard")

if(MSVC)
  target_compile_options(coorc_core PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
else()
  target_compile_options(coorc_core PRIVATE -Wall -Wextra)
endif()
