cmake_minimum_required(VERSION 3.24)

project(SkillBox VERSION 1.0.0 LANGUAGES CXX RC)

include(FetchContent)
include(CTest)

FetchContent_Declare(
    yaml_cpp
    URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.zip
    URL_HASH SHA256=334E80AB7B52E14C23F94E041C74BAB0742F2281AAD55F66BE2F19F4B7747071
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    URL https://api.nuget.org/v3-flatcontainer/nlohmann.json/3.11.3/nlohmann.json.3.11.3.nupkg
    URL_HASH SHA256=70DB5F6ADCE6852393468C7CFB540A9232A57F3AA12757F11E29F0767F9EBA68
    DOWNLOAD_NAME nlohmann-json.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
    webview2sdk
    URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4258.31/microsoft.web.webview2.1.0.4258.31.nupkg
    URL_HASH SHA256=56F7F4B8BF9AEE4B8EFEFBBDD4F67D5F74EBD1B100ED0806DA71BF76AF481AA9
    DOWNLOAD_NAME webview2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(yaml_cpp nlohmann_json webview2sdk)

add_library(WebView2 INTERFACE)
target_include_directories(WebView2 INTERFACE
    "${webview2sdk_SOURCE_DIR}/build/native/include"
)
target_link_libraries(WebView2 INTERFACE
    "${webview2sdk_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib"
)


add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    "${nlohmann_json_SOURCE_DIR}/build/native/include"
)

add_library(SkillBoxCore STATIC
    src/app/Application.cpp
    src/app/MainWindow.cpp
    src/bridge/Bridge.cpp
    src/core/ChangeMonitor.cpp
    src/core/Config.cpp
    src/core/Hash.cpp
    src/core/PathUtils.cpp
    src/core/SkillCatalog.cpp
    src/core/SkillParser.cpp
    src/net/HttpClient.cpp
    src/net/ModelClient.cpp
    src/net/TranslationOutput.cpp
    src/net/TranslationService.cpp
    src/platform/Clipboard.cpp
    src/platform/Dpapi.cpp
    src/platform/FolderPicker.cpp
)

target_compile_features(SkillBoxCore PUBLIC cxx_std_20)
target_compile_definitions(SkillBoxCore PUBLIC
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_include_directories(SkillBoxCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(SkillBoxCore PRIVATE /W4 /permissive- /utf-8)
target_compile_definitions(SkillBoxCore PRIVATE SKILLBOX_E2E=$<BOOL:${SKILLBOX_E2E}>)
endif()

target_link_libraries(SkillBoxCore PUBLIC
    yaml-cpp::yaml-cpp
    nlohmann_json
    advapi32
    bcrypt
    crypt32
    dwmapi
    gdi32
    ole32
    shell32
    user32
    winhttp
    WebView2
)

add_executable(SkillBox WIN32
    src/main.cpp
    assets/app.rc
    assets/web.rc
)

if(MSVC)
    target_compile_options(SkillBox PRIVATE /utf-8)
endif()

target_link_libraries(SkillBox PRIVATE
    SkillBoxCore
)


if(BUILD_TESTING)
    add_executable(SkillBoxTests
        tests/test_main.cpp
    )
    target_link_libraries(SkillBoxTests PRIVATE SkillBoxCore ws2_32)
    if(MSVC)
        target_compile_options(SkillBoxTests PRIVATE /W4 /utf-8)
    endif()

    add_test(NAME SkillBoxTests COMMAND SkillBoxTests)
endif()

