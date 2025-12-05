cmake_minimum_required(VERSION 3.15)
project(MINE_PROJECT VERSION 1.0.0)

if (MSVC)
    add_compile_options(/utf-8)
    add_compile_options(/wd4819)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(../JUCE JUCE)

juce_add_gui_app(MineNode
    PRODUCT_NAME "MineNode"
    VERSION 1.0.0
)

juce_generate_juce_header(MineNode)

# 包含路径：当前目录 + include目录 + JUCE生成目录
target_include_directories(MineNode PRIVATE 
    "${Project_BINARY_DIR}/JuceLibraryCode"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# 自动扫描源文件
file(GLOB SOURCE_FILES "*.cpp" "include/*.cpp" "*.h" "include/*.h")
target_sources(MineNode PRIVATE ${SOURCE_FILES})

target_link_libraries(MineNode PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_formats
)

target_compile_definitions(MineNode PRIVATE
    JUCE_ASIO=1
    JUCE_STANDALONE_APPLICATION=1
)