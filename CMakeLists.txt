cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt"
    REBALANCER_VERSION LIMIT_COUNT 1)
string(STRIP "${REBALANCER_VERSION}" REBALANCER_VERSION)

# Wheel-build hook: scikit-build-core invokes cmake from a tmp dir and can't
# easily be told about the getdeps-installed deps via env vars (cibuildwheel's
# CIBW_ENVIRONMENT does shell-substitution but its bashlex evaluator rejects
# common forms like `cat 2>/dev/null`). Instead, the wheels workflow's
# CIBW_BEFORE_ALL writes the colon-separated dep install paths to
# ./.cmake_prefix_path in the source tree (durable across cibuildwheel
# phases on macOS, where /tmp may be cleaned), and we splice them into
# CMAKE_PREFIX_PATH here.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.cmake_prefix_path")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.cmake_prefix_path" _wheel_prefix)
    string(STRIP "${_wheel_prefix}" _wheel_prefix)
    if(_wheel_prefix)
        string(REPLACE ":" ";" _wheel_prefix_list "${_wheel_prefix}")
        list(APPEND CMAKE_PREFIX_PATH ${_wheel_prefix_list})
        message(STATUS "Wheel build: appended ${_wheel_prefix_list} "
            "to CMAKE_PREFIX_PATH")
        # FindBoost looks at BOOST_ROOT before CMAKE_PREFIX_PATH; pluck the
        # boost-<hash> entry out so module-mode discovery hits the
        # getdeps-installed boost. Set BOOST_INCLUDEDIR / BOOST_LIBRARYDIR
        # explicitly too — even CMake 3.31's FindBoost can't auto-detect
        # the layout from BOOST_ROOT alone for some boost installs.
        foreach(_p IN LISTS _wheel_prefix_list)
            get_filename_component(_pname "${_p}" NAME)
            if(_pname MATCHES "^boost-")
                set(BOOST_ROOT "${_p}" CACHE PATH "" FORCE)
                set(BOOST_INCLUDEDIR "${_p}/include" CACHE PATH "" FORCE)
                set(BOOST_LIBRARYDIR "${_p}/lib" CACHE PATH "" FORCE)
                set(Boost_NO_SYSTEM_PATHS TRUE CACHE BOOL "" FORCE)
                message(STATUS "Wheel build: BOOST_ROOT=${BOOST_ROOT}")
                break()
            endif()
        endforeach()
    endif()
endif()

project(rebalancer VERSION ${REBALANCER_VERSION} LANGUAGES CXX)

# cmake_policy(SET CMP0022 NEW) cmake_policy(SET CMP0023 NEW)

# Use compiler ID "AppleClang" instead of "Clang" for XCode. Not setting this
# sometimes makes XCode C compiler gets detected as "Clang", even when the C++
# one is detected as "AppleClang".
cmake_policy(SET CMP0010 NEW)
cmake_policy(SET CMP0025 NEW)

# Enables CMake to set LTO on compilers other than Intel.
cmake_policy(SET CMP0069 NEW)
# Enable the policy for CMake subprojects. protobuf currently causes issues
# set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

# Prohibit in-source builds
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "In-source build are not supported")
endif()

if(MSVC)
    message(FATAL_ERROR
        "MSVC is not supported. Rebalancer requires a Unix-like toolchain "
        "(GCC or Clang on Linux, Apple Clang on macOS)."
    )
endif()

option(USE_GUROBI "Build with Gurobi solver" OFF)
if(USE_GUROBI)
    add_compile_definitions(REBALANCER_USE_GUROBI)
endif()

option(USE_XPRESS "Build with FICO Xpress solver" OFF)
if(USE_XPRESS)
    add_compile_definitions(REBALANCER_USE_XPRESS)
endif()

option(REBALANCER_BUILD_WEBSITE_EXAMPLES
    "Build website reference examples" OFF)
option(REBALANCER_BUILD_EXPLORER "Build Rebalancer Explorer server" OFF)

# Demo / example executables (Sudoku, NQueens, EightQueens, WebBalancing,
# ShardAllocation, Replayer, StandaloneSolver, etc.). They're useful for
# integration testing and to keep examples in sync with library API
# changes, but nothing ships them -- the wheel is data-only and the
# deb/rpm/bottle paths package only librebalancer + headers. CI test jobs
# should pass -DEXAMPLES=ON to keep building (and thus type/link-checking)
# them.
option(EXAMPLES "Build example/demo executables" OFF)

option(BENCHMARKS "Build benchmark executables" OFF)
option(INSTALL_BENCHMARKS
    "Install benchmark executables (requires BENCHMARKS=ON)" OFF)
option(TESTS "Build tests" OFF)

# Packaging smoke test (test_solve), shipped in the deb/rpm so the packaging
# jobs can verify the installed lib links and solves. On only in
# build_linux_sdk.sh.
option(PACKAGING_TEST "Build standalone packaging smoke test (test_solve)" OFF)

# Test executables (rebalancer_tests.exe + test_main) are part of the default
# ALL target but are never installed. Production/packaging builds (e.g. the
# explorer Docker image) pass -DBUILD_TESTS=OFF to skip compiling them.
# getdeps' --no-tests only skips *running* tests, not building them.
option(BUILD_TESTS "Build test executables" ON)

# Set CMAKE_MODULE_PATH to include fbcode_builder CMake modules
# This must be done before find_package calls so that FBThrift's
# find_dependency(Xxhash) can find FindXxhash.cmake
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/CMake"
    # For in-fbsource builds
    "${CMAKE_CURRENT_SOURCE_DIR}/../opensource/fbcode_builder/CMake"
    # For shipit-transformed builds
    "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
)

# Also add to CMAKE_PREFIX_PATH for getdeps-installed dependencies
list(APPEND CMAKE_PREFIX_PATH
    # For in-fbsource builds
    "${CMAKE_CURRENT_SOURCE_DIR}/../opensource/fbcode_builder/CMake"
    # For shipit-transformed builds
    "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
)

find_package(Boost REQUIRED COMPONENTS thread)
find_package(fmt CONFIG REQUIRED)
find_package(folly CONFIG REQUIRED)
# RPC-enabled FBThrift's exported targets reference fizz/wangle/mvfst but its
# config doesn't find_dependency() them, so locate them before
# find_package(FBThrift).
if(REBALANCER_BUILD_EXPLORER)
    find_package(fizz CONFIG REQUIRED)
    find_package(wangle CONFIG REQUIRED)
    find_package(mvfst CONFIG REQUIRED)
endif()
find_package(FBThrift REQUIRED)
find_package(highs CONFIG)
if(REBALANCER_BUILD_EXPLORER)
    find_package(gflags CONFIG REQUIRED)
    find_package(Re2 REQUIRED)
    # Needed by the HTTP/JSON proxy (rebalancer_explorer_proxy) below.
    find_package(proxygen CONFIG REQUIRED)
endif()

find_package(glog CONFIG REQUIRED)

set(THRIFT_COMPILER "${FBTHRIFT_COMPILER}")

message(STATUS "FBTHRIFT_COMPILER: ${FBTHRIFT_COMPILER}")
message(STATUS "FBTHRIFT_INCLUDE_DIR: ${FBTHRIFT_INCLUDE_DIR}")

set(CMAKE_CXX_STANDARD
    20
    CACHE STRING
    "The C++ standard whose features are requested to build this target.")

string(APPEND CMAKE_CXX_FLAGS
    " -Wno-nullability"
    " -Wno-deprecated-declarations"
    " -Wno-nullability-completeness")
# Enable Clang's compile-time thread safety analysis when available.
# The REBALANCER_GUARDED_BY / REBALANCER_EXCLUDES macros in
# common/ThreadAnnotations.h are no-ops on compilers that lack support.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wthread-safety")
endif()
# Homebrew's x86_64 folly bottle is compiled with AVX2; F14's link-time
# check requires the consumer to match. Only applies on macOS/Intel —
# Linux builds folly from source via getdeps without AVX2.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
    endif()
endif()

# Used to make <algopt/rebalancer/...> a viable include path.
set(THRIFT_OUT "${CMAKE_CURRENT_BINARY_DIR}/thrift_out")
file(MAKE_DIRECTORY ${THRIFT_OUT})
# algopt/ roots let explorer sources resolve their "rebalancer/explorer/..."
# includes (source tree + thrift_out).
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${THRIFT_OUT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/algopt" "${THRIFT_OUT}/algopt"
)

add_definitions(-DREBALANCER_OSS_BUILD)

# A solver that fails to load falls back to HiGHS, never to the other
# commercial solver; see ProblemFactory.cpp.
add_compile_definitions(REBALANCER_SOLVER_FALLBACK_TO_HIGHS_ONLY)

option(USE_HIGHS "Build with HiGHS solver" OFF)
if(highs_FOUND OR USE_HIGHS)
    add_compile_definitions(REBALANCER_USE_HIGHS)
    message(STATUS "Building Rebalancer with HiGHS support.")
else()
    message(WARNING
        "HiGHS not found. Building Rebalancer without HiGHS support.")
endif()

set(CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use lld linker if available for faster linking.
# lld is faster and more memory-efficient than the default ld.bfd.
find_program(LLD_LINKER ld.lld)
if(LLD_LINKER)
    add_link_options("-fuse-ld=lld")
    message(STATUS "Using lld linker for faster linking")
endif()


################################################
# Generate Thrift code
################################################
set(THRIFT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/thrift_out")
file(MAKE_DIRECTORY ${THRIFT_OUTPUT_DIR})


# Gather the Thrift files and generate them. This isn't the most ergonomic
# setup because we would like to be able to edit these files without rerunning
# cmake setup. But it will work for now. TODO: Fix this.
file(GLOB_RECURSE thrift_files "*.thrift")
foreach(FILE ${thrift_files})
    cmake_path(GET FILE PARENT_PATH FILE_PATH)
    cmake_path(GET FILE FILENAME FILE_NAME)

    file(RELATIVE_PATH RELATIVE_FILE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}" "${FILE_PATH}")
    set(OUTPUT_PATH "${THRIFT_OUTPUT_DIR}/${RELATIVE_FILE_PATH}")
    file(MAKE_DIRECTORY ${OUTPUT_PATH})

    message(STATUS
        "${THRIFT_COMPILER} --gen "
        "mstch_cpp2:json,include_prefix=${RELATIVE_FILE_PATH} "
        "-o ${OUTPUT_PATH} -I ${CMAKE_CURRENT_BINARY_DIR} "
        "-I ${FBTHRIFT_INCLUDE_DIR} ${FILE}"
      )

    execute_process(
        COMMAND ${THRIFT_COMPILER}
          --gen "mstch_cpp2:json,include_prefix=${RELATIVE_FILE_PATH}"
          -o "${OUTPUT_PATH}"
          -I "${CMAKE_CURRENT_SOURCE_DIR}"
          -I "${CMAKE_CURRENT_BINARY_DIR}"
          -I "${FBTHRIFT_INCLUDE_DIR}"
          ${FILE}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      )
endforeach()


################################################
# Categorize source files
################################################

function(categorize_source_files
    INPUT_FILES
    TEST_FILES
    BENCHMARK_FILES
    EXAMPLE_FILES
    EXECUTABLE_FILES
    LIBRARY_FILES)
    # Clear output lists
    set(tests "")
    set(benchmarks "")
    set(examples "")
    set(executables "")
    set(libraries "")

    foreach(file ${INPUT_FILES})
        # File contains int main
        get_filename_component(filename ${file} NAME)

        string(FIND "${filename}" "test_main.cpp" is_test_main)
        if(is_test_main GREATER -1)
            message(STATUS "Skipping test_main file: ${file}")
            continue()
        endif()

        # Files ending in "Test.cpp" are test files, but test utility/helper
        # files (e.g. "TestUtils.cpp") are library files, not standalone tests.
        string(REGEX MATCH "Tests?\\.(cpp|cc)$" is_test_file "${filename}")
        if(is_test_file)
            message(STATUS "Identified test file: ${file}")
            list(APPEND tests ${file})
            continue()
        endif()

        # Files in tests/ directories are test helpers (base classes, mocks,
        # utils) that must be compiled with the test binary, not the library.
        string(FIND "${file}" "/tests/" is_in_tests_dir)
        if(is_in_tests_dir GREATER -1)
            message(STATUS "Identified test helper file: ${file}")
            list(APPEND tests ${file})
            continue()
        endif()

        string(FIND "${file}" "benchmarks/" is_in_benchmarks_dir)
        if(is_in_benchmarks_dir GREATER -1)
            message(STATUS "Identified benchmark file: ${file}")
            list(APPEND benchmarks ${file})
            continue()
        endif()

        string(FIND "${file}" "examples/" is_example)
        if(is_example GREATER -1)
            message(STATUS "Identified example file: ${file}")
            list(APPEND examples ${file})
            continue()
        endif()

        # Read file content
        file(READ ${file} file_content)

        # Check if file contains 'int main'
        string(FIND "${file_content}" "int main" has_main)

        if(has_main GREATER -1)
            # Regular executables (have int main, no other matches)
            message(STATUS "Identified executable file: ${file}")
            list(APPEND executables ${file})
        else()
            # File doesn't contain int main - it's a library file
            message(STATUS "Identified library file: ${file}")
            list(APPEND libraries ${file})
        endif()
    endforeach()

    # Set output variables in parent scope
    set(${TEST_FILES} ${tests} PARENT_SCOPE)
    set(${BENCHMARK_FILES} ${benchmarks} PARENT_SCOPE)
    set(${EXAMPLE_FILES} ${examples} PARENT_SCOPE)
    set(${EXECUTABLE_FILES} ${executables} PARENT_SCOPE)
    set(${LIBRARY_FILES} ${libraries} PARENT_SCOPE)
endfunction()

# Collect all source files
file(GLOB_RECURSE all_cpp_files "*.cpp")
message(STATUS "All source files: ${all_cpp_files}")

# Rebalancer Explorer is exported into this repository, but it has its own
# packaging/dependency manifest and is not part of the core librebalancer
# build.
list(FILTER all_cpp_files EXCLUDE REGEX
    "rebalancer/explorer/")

# These tests exercise the Xpress C++ SDK directly and need Meta's internal
# license setup, so they cannot build outside fbcode.
list(FILTER all_cpp_files EXCLUDE REGEX
    "algopt/lp/test/xpress_sdk(_creator)?/")

# Optionally exclude website reference examples (broken/WIP)
if(NOT REBALANCER_BUILD_WEBSITE_EXAMPLES)
    list(FILTER all_cpp_files EXCLUDE REGEX "examples/website/")
endif()

# Exclude the nanobind Python extension's translation unit. It pulls in
# <nanobind/nanobind.h> and is built separately below (SKBUILD path), not
# bundled into librebalancer.
list(FILTER all_cpp_files EXCLUDE REGEX
    "algopt/rebalancer/python/Bindings\\.cpp$")

# Categorize the files
categorize_source_files(
    "${all_cpp_files}"
    rebalancer_test_files
    rebalancer_benchmark_files
    rebalancer_example_files
    rebalancer_exe_files
    rebalancer_library_files
)

# examples/common/ are shared helpers, not standalone examples. The categorizer
# treats everything under examples/ as one, so split them out for a support lib
# the examples link against instead of each becoming its own (broken) .exe.
set(rebalancer_examples_common_files ${rebalancer_example_files})
list(FILTER rebalancer_examples_common_files INCLUDE REGEX "examples/common/")
list(FILTER rebalancer_example_files EXCLUDE REGEX "examples/common/")

# Collect thrift-generated .cpp files and add to the library sources
file(GLOB_RECURSE thrift_gen_cpp_files "${THRIFT_OUTPUT_DIR}/*.cpp")
set(explorer_thrift_gen_cpp_files ${thrift_gen_cpp_files})
list(FILTER explorer_thrift_gen_cpp_files INCLUDE REGEX
    "[/\\\\]rebalancer[/\\\\]explorer[/\\\\]")
list(FILTER thrift_gen_cpp_files EXCLUDE REGEX
    "[/\\\\]rebalancer[/\\\\]explorer[/\\\\]")
list(LENGTH thrift_gen_cpp_files thrift_gen_count)
message(STATUS "Found ${thrift_gen_count} thrift-generated .cpp files")
list(APPEND rebalancer_library_files ${thrift_gen_cpp_files})

################################################
# Create libraries and executables
################################################

function(create_executables TARGET_NAME EXECUTABLE_FILES)
    # Supports an optional NO_INSTALL flag; all other extra args are link libs.
    cmake_parse_arguments(CE "NO_INSTALL" "" "" ${ARGN})
    set(link_libraries ${CE_UNPARSED_ARGUMENTS})

    set(executable_targets "")

    foreach(exec_file ${EXECUTABLE_FILES})
        # Get executable name from filename (without extension)
        get_filename_component(exec_name ${exec_file} NAME_WE)

        # Skip if this target name already exists
        if(TARGET "${exec_name}.exe")
            message(WARNING
                "Target ${exec_name}.exe already exists. Skipping creation.")
            continue()
        endif()

        message(STATUS
            "Creating executable target: ${exec_name}.exe "
            "from source file: ${exec_file}")
        add_executable("${exec_name}.exe" ${exec_file})
        target_include_directories("${exec_name}.exe"
            PUBLIC
                ${FBTHRIFT_INCLUDE_DIR}
                ${CMAKE_CURRENT_BINARY_DIR}
                ${THRIFT_OUTPUT_DIR}
        )

        # Link against all provided libraries
        if(link_libraries)
            target_link_libraries("${exec_name}.exe" PRIVATE ${link_libraries})
        endif()

        if(NOT CE_NO_INSTALL)
            install(TARGETS "${exec_name}.exe" RUNTIME DESTINATION bin)
        endif()

        # Track this executable
        list(APPEND executable_targets "${exec_name}.exe")
    endforeach()

    # Create a custom target that builds all executables
    if(executable_targets)
        add_custom_target(${TARGET_NAME}
            DEPENDS ${executable_targets}
            COMMENT "Building all ${TARGET_NAME}"
        )

        message(STATUS
            "Created meta-target '${TARGET_NAME}' target with: "
            "${executable_targets}")
    endif()
endfunction()

add_library(rebalancer SHARED ${rebalancer_library_files})
target_link_libraries(rebalancer
    PUBLIC
        Folly::folly
        FBThrift::thriftprotocol
        FBThrift::thriftmetadata
        FBThrift::thrifttype
        FBThrift::rpcmetadata
        fmt::fmt
)
target_include_directories(rebalancer
    PUBLIC
        ${FBTHRIFT_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${THRIFT_OUTPUT_DIR}
)

if(highs_FOUND OR USE_HIGHS)
    target_link_libraries(rebalancer PRIVATE highs::highs)
endif()

if(REBALANCER_BUILD_EXPLORER)
    set(_exp_cpp algopt/rebalancer/explorer/cpp_server)
    set(rebalancer_explorer_sources
        ${_exp_cpp}/RebalancerExplorerHandlerBase.cpp
        ${_exp_cpp}/lib/FilterModel.cpp
        ${_exp_cpp}/lib/GroupModel.cpp
        ${_exp_cpp}/lib/LoadModel.cpp
        ${_exp_cpp}/lib/MetricsTabulator.cpp
        ${_exp_cpp}/lib/TableStore.cpp
        ${_exp_cpp}/lib/Utils.cpp
        ${_exp_cpp}/server/ExplorerServerHandler.cpp
        ${_exp_cpp}/server/ModelServer.cpp
        ${_exp_cpp}/server/FileSandboxFactory.cpp
        ${_exp_cpp}/server/StandaloneExplorerServiceHandler.cpp
    )

    add_library(
        rebalancer_explorer
        ${rebalancer_explorer_sources}
        ${explorer_thrift_gen_cpp_files}
    )
    target_link_libraries(rebalancer_explorer
        PUBLIC
            rebalancer
            FBThrift::thriftcpp2
            Folly::folly
            fmt::fmt
            ${RE2_LIBRARY}
    )
    target_include_directories(rebalancer_explorer
        PUBLIC
            ${FBTHRIFT_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${THRIFT_OUTPUT_DIR}
            ${RE2_INCLUDE_DIR}
    )

    add_executable(
        rebalancer_explorer_server.exe
        ${_exp_cpp}/server/StandaloneExplorerService.cpp
    )
    set_target_properties(
        rebalancer_explorer_server.exe
        PROPERTIES OUTPUT_NAME "rebalancer_explorer_server"
    )
    target_link_libraries(rebalancer_explorer_server.exe
        PRIVATE
            rebalancer_explorer
            FBThrift::thriftcpp2
            Folly::folly
            fmt::fmt
            gflags
            glog::glog
    )
    target_include_directories(rebalancer_explorer_server.exe
        PRIVATE
            ${FBTHRIFT_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${THRIFT_OUTPUT_DIR}
    )
    if(APPLE)
        set(_rebalancer_explorer_loader_rpath "@loader_path/../lib")
    else()
        set(_rebalancer_explorer_loader_rpath "$ORIGIN/../lib")
    endif()
    set_property(TARGET rebalancer_explorer_server.exe APPEND PROPERTY
        BUILD_RPATH "${_rebalancer_explorer_loader_rpath}"
    )
    set_property(TARGET rebalancer_explorer_server.exe APPEND PROPERTY
        INSTALL_RPATH "${_rebalancer_explorer_loader_rpath}"
    )
    # Install so fixup-dyn-deps bundles it; the server loads it via RPATH.
    install(TARGETS rebalancer_explorer
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
    install(TARGETS rebalancer_explorer_server.exe RUNTIME DESTINATION bin)

    # ----- HTTP/JSON proxy in front of the Explorer Thrift service -----
    # External callers POST /v2/<method> with JSON; the proxy transcodes
    # JSON <-> Thrift and forwards to a single configured Explorer backend.
    # Sources live alongside the rest of the explorer (rebalancer/explorer/
    # proxy/); the paths below use the same prefix as the explorer sources
    # above.
    add_executable(
        rebalancer_explorer_proxy.exe
        algopt/rebalancer/explorer/proxy/RebalancerExplorerProxy.cpp
        algopt/rebalancer/explorer/proxy/JsonProxyHandler.cpp
    )
    set_target_properties(
        rebalancer_explorer_proxy.exe
        PROPERTIES OUTPUT_NAME "rebalancer_explorer_proxy"
    )
    target_link_libraries(rebalancer_explorer_proxy.exe
        PRIVATE
            rebalancer_explorer
            proxygen::proxygenhttpserver
            proxygen::proxygen
            FBThrift::thriftcpp2
            Folly::folly
            fmt::fmt
            gflags
            glog::glog
    )
    target_include_directories(rebalancer_explorer_proxy.exe
        PRIVATE
            ${FBTHRIFT_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${THRIFT_OUTPUT_DIR}
    )
    # getdeps builds c-ares as a static archive off the default linker search
    # path, so proxygen's transitive DNS dependency can fail to resolve at link
    # time. Link it explicitly when found. (getdeps/OSS-only; Buck resolves
    # proxygen's transitive deps itself, so there is no fbcode equivalent.)
    find_library(CARES_LIBRARY NAMES cares c-ares)
    if(CARES_LIBRARY)
        target_link_libraries(
            rebalancer_explorer_proxy.exe PRIVATE ${CARES_LIBRARY})
    endif()
    set_property(TARGET rebalancer_explorer_proxy.exe APPEND PROPERTY
        BUILD_RPATH "${_rebalancer_explorer_loader_rpath}"
    )
    set_property(TARGET rebalancer_explorer_proxy.exe APPEND PROPERTY
        INSTALL_RPATH "${_rebalancer_explorer_loader_rpath}"
    )
    install(TARGETS rebalancer_explorer_proxy.exe RUNTIME DESTINATION bin)
endif()

# Helper lib (maybeSaveBundle, the Sudoku problem builder). Every example links
# it, and so do ExamplesBenchmark.exe and rebalancer_tests.exe, hence the gate
# is wider than EXAMPLES.
if(EXAMPLES OR TESTS OR BENCHMARKS)
    add_library(rebalancer_examples_common STATIC
        ${rebalancer_examples_common_files})
    target_link_libraries(rebalancer_examples_common
        PUBLIC rebalancer Folly::folly)
    target_include_directories(rebalancer_examples_common
        PUBLIC
            ${FBTHRIFT_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${THRIFT_OUTPUT_DIR}
    )
endif()

if(EXAMPLES)
    create_executables(rebalancer_exes "${rebalancer_exe_files}"
        rebalancer rebalancer_examples_common Folly::folly)
    create_executables(rebalancer_examples "${rebalancer_example_files}"
        rebalancer rebalancer_examples_common Folly::folly)
endif()

if(BUILD_TESTS)
# GTest is needed by the test binary (TESTS) and is also linked by the
# benchmark executables (see benchmark_link_libs below), so resolve it
# whenever either is enabled.
if(TESTS OR BENCHMARKS)
    find_package(GTest REQUIRED)
    include(GoogleTest)
endif()

if(TESTS)
    # Find test_main.cpp from source tree,
    # path varies between in-fbsource and shipit builds.
    file(GLOB_RECURSE test_main_file "*/test_main.cpp")
    # Get the first match
    list(GET test_main_file 0 test_main_file)
    if(NOT test_main_file)
        message(FATAL_ERROR "Could not find test_main.cpp in source tree")
    endif()
    add_library(
        test_main
        ${test_main_file}
    )
    target_link_libraries(test_main
        PUBLIC
            Folly::folly
            GTest::gtest
    )
endif()

if(BENCHMARKS)
    # Split benchmark files into executables (have int main) and support
    # libraries
    set(rebalancer_benchmark_exe_files "")
    set(rebalancer_benchmark_lib_files "")
    foreach(file ${rebalancer_benchmark_files})
        file(READ ${file} file_content)
        string(FIND "${file_content}" "int main" has_main)
        if(has_main GREATER -1)
            list(APPEND rebalancer_benchmark_exe_files ${file})
        else()
            list(APPEND rebalancer_benchmark_lib_files ${file})
        endif()
    endforeach()

    # Build benchmark support library (BenchmarkUtils, etc.)
    if(rebalancer_benchmark_lib_files)
        add_library(rebalancer_benchmark_support
            ${rebalancer_benchmark_lib_files})
        target_link_libraries(rebalancer_benchmark_support
            PUBLIC
                rebalancer
                Folly::follybenchmark
        )
        target_include_directories(rebalancer_benchmark_support
            PUBLIC
                ${FBTHRIFT_INCLUDE_DIR}
                ${CMAKE_CURRENT_BINARY_DIR}
                ${THRIFT_OUTPUT_DIR}
        )
    endif()

    set(benchmark_link_libs
        rebalancer rebalancer_examples_common Folly::follybenchmark
        GTest::gtest Boost::thread)
    if(rebalancer_benchmark_lib_files)
        list(APPEND benchmark_link_libs rebalancer_benchmark_support)
    endif()

    if(INSTALL_BENCHMARKS)
        create_executables(rebalancer_benchmarks
            "${rebalancer_benchmark_exe_files}" ${benchmark_link_libs})
    else()
        create_executables(rebalancer_benchmarks
            "${rebalancer_benchmark_exe_files}"
            NO_INSTALL ${benchmark_link_libs})
    endif()
endif()

if(TESTS)
    add_executable(rebalancer_tests.exe ${rebalancer_test_files})
    target_link_libraries(rebalancer_tests.exe
        PUBLIC
            rebalancer
            rebalancer_examples_common
            Folly::follybenchmark
            FBThrift::thriftprotocol
            test_main
            GTest::gtest
            GTest::gmock
            Boost::thread
    )
    target_include_directories(rebalancer_tests.exe
        PUBLIC
            ${FBTHRIFT_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${THRIFT_OUTPUT_DIR}
    )
    if(highs_FOUND OR USE_HIGHS)
        target_link_libraries(rebalancer_tests.exe PUBLIC highs::highs)
    endif()
    # rebalancer_tests.exe links against librebalancer (a shared library
    # living in the build tree alongside the test binary). Tell the dynamic
    # loader to look there at run time so gtest_discover_tests / ctest can
    # launch the binary without a separately-installed librebalancer.
    #
    # Append to BOTH BUILD_RPATH and INSTALL_RPATH because getdeps sets
    # CMAKE_BUILD_WITH_INSTALL_RPATH=ON on macOS, in which case the
    # build-tree binary uses INSTALL_RPATH at link time. On Linux the
    # build-tree binary uses BUILD_RPATH instead.
    if(APPLE)
        set(_rebalancer_loader_rpath "@loader_path")
    else()
        set(_rebalancer_loader_rpath "$ORIGIN")
    endif()
    set_property(TARGET rebalancer_tests.exe APPEND PROPERTY
        BUILD_RPATH "${_rebalancer_loader_rpath}"
    )
    set_property(TARGET rebalancer_tests.exe APPEND PROPERTY
        INSTALL_RPATH "${_rebalancer_loader_rpath}"
    )

    # Enable CTest and register tests for GitHub Actions
    enable_testing()
    gtest_discover_tests(rebalancer_tests.exe
      DISCOVERY_MODE PRE_TEST
      PROPERTIES TIMEOUT 120
    )

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(
            NAME check_benchmark_paths
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_benchmark_paths.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/algopt"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()
endif()
endif() # BUILD_TESTS

################################################
# Install targets
################################################

if(NOT SKBUILD)
    install(TARGETS rebalancer
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )

    # Install source headers and thrift-generated headers for the dev-build
    # (deb/rpm/bottle) path. The wheel build is data-only and only needs the
    # shared library, installed below into the Python package layout.
    install(DIRECTORY algopt/ DESTINATION include/algopt
            FILES_MATCHING PATTERN "*.h")

    install(DIRECTORY ${THRIFT_OUTPUT_DIR}/algopt/ DESTINATION include/algopt
            FILES_MATCHING PATTERN "*.h" PATTERN "*.tcc")

    # Install thrift-generated headers for stub thrift files that live outside
    # algopt/ (e.g. configerator/structs/thrift_explorer/). The generated C++
    # headers for our .thrift files include these transitively, so SDK users
    # need them to compile against the installed headers.
    install(DIRECTORY ${THRIFT_OUTPUT_DIR}/configerator/
            DESTINATION include/configerator
            FILES_MATCHING PATTERN "*.h" PATTERN "*.tcc")

    # Smoke test binary, installed to bin/. INSTALL_RPATH lets it find the
    # shared lib from the install tree (build_linux_sdk.sh patches the same
    # path).
    if(PACKAGING_TEST)
        add_executable(test_solve tools/packages/test_solve.cpp)
        # test_solve.cpp transitively instantiates folly coro / F14 templates
        # whose non-inline symbols live in libfolly.so. Add folly and the
        # fbthrift type-system libs explicitly so the linker resolves them.
        target_link_libraries(test_solve PRIVATE
            rebalancer
            Folly::folly
            FBThrift::thriftprotocol
            FBThrift::thriftmetadata
            FBThrift::thrifttype
            FBThrift::rpcmetadata
        )
        if(APPLE)
            set_property(TARGET test_solve PROPERTY INSTALL_RPATH
                "@loader_path/../lib"
                "@loader_path/../lib/rebalancer")
        else()
            set_property(TARGET test_solve PROPERTY INSTALL_RPATH
                "$ORIGIN/../lib:$ORIGIN/../lib/rebalancer")
        endif()
        install(TARGETS test_solve RUNTIME DESTINATION bin)
    endif()
endif()

# When invoked by scikit-build-core (during a `pip install` / wheel build),
# also install librebalancer.{so,dylib} into the Python package layout so
# auditwheel/delocate can bundle it. SKBUILD is set by scikit-build-core
# only; a normal cmake invocation leaves these targets untouched.
if(SKBUILD)
    install(TARGETS rebalancer
        LIBRARY DESTINATION ${SKBUILD_PROJECT_NAME}/_lib
        ARCHIVE DESTINATION ${SKBUILD_PROJECT_NAME}/_lib
    )

    ############################################
    # nanobind Python extension (_rebalancer)
    ############################################
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_rebalancer
        NB_STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/algopt/rebalancer/python/Bindings.cpp
    )
    target_link_libraries(_rebalancer PRIVATE rebalancer)
    target_include_directories(_rebalancer
        PRIVATE
            ${FBTHRIFT_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${THRIFT_OUTPUT_DIR}
    )

    # macOS wheel rpaths so delocate can resolve @rpath/... loads from baked-in
    # LC_RPATH: USE_LINK_PATH exposes the getdeps dep dirs (libfolly, ...) for
    # bundling; @loader_path/_lib finds the sibling librebalancer.dylib.
    if(APPLE)
        set_property(TARGET rebalancer PROPERTY INSTALL_RPATH_USE_LINK_PATH ON)
        set_target_properties(_rebalancer PROPERTIES
            INSTALL_RPATH_USE_LINK_PATH ON
            INSTALL_RPATH "@loader_path/_lib")
    endif()

    # Install the extension into the wheel package; the wrapper __init__.py
    # already lives in src/rebalancer/ (picked up via wheel.packages in
    # pyproject.toml). Ship the type stub from the in-tree binding source.
    install(TARGETS _rebalancer LIBRARY DESTINATION ${SKBUILD_PROJECT_NAME})
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/algopt/rebalancer/python/_rebalancer.pyi
        DESTINATION ${SKBUILD_PROJECT_NAME}
    )
endif()

################################################
# Print all targets
################################################

function(print_all_targets DIR)
    get_property(TGTS DIRECTORY "${DIR}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(TGT IN LISTS TGTS)
        message(STATUS "Target: ${TGT}")
        # TODO: Do something about it
    endforeach()

    get_property(SUBDIRS DIRECTORY "${DIR}" PROPERTY SUBDIRECTORIES)
    foreach(SUBDIR IN LISTS SUBDIRS)
        print_all_targets("${SUBDIR}")
    endforeach()
endfunction()

print_all_targets(.)
