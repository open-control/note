cmake_minimum_required(VERSION 3.29)

project(open_control_note LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(OPEN_CONTROL_FRAMEWORK_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../framework"
    CACHE PATH "Path to the local open-control framework repository")

if(NOT TARGET oc_framework_native AND NOT EXISTS "${OPEN_CONTROL_FRAMEWORK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "open-control framework CMake harness not found at ${OPEN_CONTROL_FRAMEWORK_DIR}")
endif()

if(NOT TARGET oc_framework_native)
    add_subdirectory(
        "${OPEN_CONTROL_FRAMEWORK_DIR}"
        "${CMAKE_BINARY_DIR}/open-control-framework")
endif()

file(GLOB_RECURSE OC_NOTE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(oc_note_native STATIC ${OC_NOTE_SOURCES})
target_include_directories(oc_note_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(oc_note_native PUBLIC oc_framework_native)

set(OC_NOTE_BUILD_TESTS_DEFAULT OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(OC_NOTE_BUILD_TESTS_DEFAULT ON)
endif()

option(
    OC_NOTE_BUILD_TESTS
    "Build and register OpenControl note unit tests"
    ${OC_NOTE_BUILD_TESTS_DEFAULT})

include(CTest)

if(OC_NOTE_BUILD_TESTS AND BUILD_TESTING)
    if(NOT TARGET unity)
        message(FATAL_ERROR "MS_UNITY_SOURCE_DIR is required for tests. Run tests through `uv run ms test ...`.")
    endif()

    file(GLOB OC_NOTE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_*/*.cpp")

    set(OC_NOTE_TEST_TARGETS)

    foreach(test_source IN LISTS OC_NOTE_TEST_SOURCES)
        get_filename_component(test_dir "${test_source}" DIRECTORY)
        get_filename_component(test_name "${test_dir}" NAME)
        set(test_target "oc_note_${test_name}")
        list(APPEND OC_NOTE_TEST_TARGETS "${test_target}")

        add_executable("${test_target}" "${test_source}")
        target_link_libraries("${test_target}" PRIVATE oc_note_native oc_framework_native unity)
        target_include_directories("${test_target}"
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/test"
                "${CMAKE_CURRENT_SOURCE_DIR}/src")

        add_test(NAME "${test_name}" COMMAND "${test_target}")
        set_tests_properties("${test_name}" PROPERTIES LABELS open-control-note)
    endforeach()

    if(PROJECT_IS_TOP_LEVEL)
        add_custom_target(test-native
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --parallel --output-on-failure
            DEPENDS ${OC_NOTE_TEST_TARGETS}
            USES_TERMINAL)
    endif()
endif()
