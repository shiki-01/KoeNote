cmake_minimum_required(VERSION 3.22)

project(KoeNote VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(KOENOTE_BUILD_PLUGIN "VST3 プラグイン本体をビルドする" ON)
option(KOENOTE_BUILD_TESTS "Catch2 単体テストをビルドする" ON)
option(KOENOTE_COPY_PLUGIN "ビルド後に VST3 をユーザーの VST3 フォルダへコピーする" OFF)

# 依存ライブラリは external/ 以下の git submodule（JUCE 8.0.x、ARA SDK 2.3、Catch2 3.x）
if(KOENOTE_BUILD_PLUGIN)
    add_subdirectory(external/JUCE)
    # ARA SDK は「Umbrella installer」形式（ARA_SDK ディレクトリ直下を指定する）
    juce_set_ara_sdk_path(${CMAKE_CURRENT_SOURCE_DIR}/external/ARA_SDK)
endif()

if(KOENOTE_BUILD_TESTS)
    add_subdirectory(external/Catch2)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/Catch2/extras)
    include(CTest)
    include(Catch)
endif()

# JUCE に依存しない純粋な C++20 のコア（解析 interface / 編集モデル / レンダリング）
add_subdirectory(src)

if(KOENOTE_BUILD_PLUGIN)
    add_subdirectory(src/plugin)
endif()

if(KOENOTE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
