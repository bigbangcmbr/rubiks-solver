cmake_minimum_required(VERSION 3.20)
project(rubiks_ai LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rubiks_core src/cube.cpp src/dataset.cpp)
target_include_directories(rubiks_core PUBLIC include)
add_executable(rubiks src/main.cpp)
target_link_libraries(rubiks PRIVATE rubiks_core)

option(RUBIKS_BUILD_VIEWER "Build the Vulkan + Dear ImGui interactive viewer" ON)
if(RUBIKS_BUILD_VIEWER)
  find_package(Vulkan REQUIRED)
  find_package(glfw3 CONFIG REQUIRED)
  find_program(GLSLC glslc REQUIRED)

  include(FetchContent)
  FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(imgui)
  add_library(dear_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
  target_include_directories(dear_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(dear_imgui PUBLIC Vulkan::Vulkan glfw)

  set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  file(MAKE_DIRECTORY ${SHADER_DIR})
  foreach(shader cube.vert cube.frag)
    add_custom_command(OUTPUT ${SHADER_DIR}/${shader}.spv
      COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
              -o ${SHADER_DIR}/${shader}.spv
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
      VERBATIM)
    list(APPEND SHADER_BINARIES ${SHADER_DIR}/${shader}.spv)
  endforeach()
  add_custom_target(rubiks_shaders DEPENDS ${SHADER_BINARIES})

  add_executable(rubiks_viewer src/viewer.cpp src/cube_renderer.cpp)
  add_dependencies(rubiks_viewer rubiks_shaders)
  target_include_directories(rubiks_viewer PRIVATE include)
  target_link_libraries(rubiks_viewer PRIVATE rubiks_core dear_imgui Vulkan::Vulkan glfw)
  target_compile_definitions(rubiks_viewer PRIVATE
    RUBIKS_VERTEX_SHADER="${SHADER_DIR}/cube.vert.spv"
    RUBIKS_FRAGMENT_SHADER="${SHADER_DIR}/cube.frag.spv")
endif()
