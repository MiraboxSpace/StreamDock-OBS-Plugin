cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Prefer project-provided CMake modules before dependency-provided modules.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

if(APPLE)
  # Homebrew Qt's FindWrapOpenGL may cache AGL.framework and force-link it,
  # but newer macOS SDKs no longer provide AGL. Point this cache variable to
  # OpenGL.framework to avoid injecting an unavailable framework name.
  set(WrapOpenGL_AGL "/System/Library/Frameworks/OpenGL.framework" CACHE FILEPATH "Compatibility override for Qt WrapOpenGL on modern macOS SDKs" FORCE)
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)

if(APPLE)
  # Newer macOS SDKs do not ship AGL.framework; remove stale transitive links.
  get_target_property(_libobs_links OBS::libobs INTERFACE_LINK_LIBRARIES)
  if(_libobs_links)
    list(FILTER _libobs_links EXCLUDE REGEX "AGL(\\.framework)?$")
    set_property(TARGET OBS::libobs PROPERTY INTERFACE_LINK_LIBRARIES "${_libobs_links}")
  endif()
endif()

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.cpp src/ui/about-dialog.cpp
          src/ui/about.qrc src/websocket-server.cpp src/json-rpc.cpp
          src/event-manager.cpp
          src/handlers/handler-system.cpp src/handlers/handler-obs-frontend.cpp
          src/handlers/handler-obs-source.cpp src/handlers/handler-obs-scene.cpp
          src/handlers/handler-events.cpp
          include/ui/about-dialog.h include/json-rpc.h include/event-manager.h
          include/handlers/handler-obs-source.h include/handlers/handler-obs-scene.h
          include/handlers/handler-events.h
          )
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(
  ${CMAKE_PROJECT_NAME}
  SYSTEM PRIVATE third_party/websocketpp-0.8.2 third_party/asio-1.28.0/include third_party)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE plugin-support)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE $<$<BOOL:${ENABLE_QT}>:ENABLE_QT>)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)

  if(APPLE)
    get_target_property(_frontend_links OBS::obs-frontend-api INTERFACE_LINK_LIBRARIES)
    if(_frontend_links)
      list(FILTER _frontend_links EXCLUDE REGEX "AGL(\\.framework)?$")
      set_property(TARGET OBS::obs-frontend-api PROPERTY INTERFACE_LINK_LIBRARIES "${_frontend_links}")
    endif()
  endif()

  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# WebSocket server 需要的库
if(WIN32)
  # Windows 下使用 w32-pthreads
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  # 链接 Windows socket 库
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 wsock32)
  # 禁用 websocketpp 的 C4267 警告 (size_t 到 uint32_t 转换) F**k Microsoft
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /wd4267)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
