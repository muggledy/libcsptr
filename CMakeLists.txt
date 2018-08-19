cmake_minimum_required(VERSION 2.8)

project(csptr C)

# Project setup & environment variables

enable_testing()

set(PROJECT_VERSION "2.0.5-7")
set(MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cmake/Modules")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${MODULE_DIR})

if (NOT MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g -std=gnu99 -Wno-unused-result -Wno-missing-field-initializers")
endif ()

if (WIN32 AND NOT MSVC)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-no-undefined")
endif ()

# Setup coveralls

option(COVERALLS "Turn on coveralls support" OFF)
option(COVERALLS_UPLOAD "Upload the generated coveralls json" ON)

if (COVERALLS AND NOT LIBCSPTR_DISABLE_COVERALLS)
    include(Coveralls)
    coveralls_turn_on_coverage()
endif ()

# Find dependencies

option(LIBCSPTR_TESTS "Turn on tests" ON)

find_package(Check)
if (CHECK_FOUND AND LIBCSPTR_TESTS AND NOT LIBCSPTR_DISABLE_TESTS)
    add_subdirectory(check)
endif ()

# Setup options

option(SENTINEL "Use a sentinel for the variadic function arguments" ON)
option(FIXED_ALLOCATOR "Define if malloc should always be used" OFF)


# List sources and headers
set(SOURCE_FILES include/csptr_smart_ptr.h)

set(MAN_FILES doc/csptr.3)

if (NOT WIN32)
    add_definitions("-fPIC")
endif ()
add_definitions(-DCSPTR_SMART_PTR_IMPLEMENTATION)
include_directories(include)

if (COVERALLS AND NOT LIBCSPTR_DISABLE_COVERALLS)
    coveralls_setup("${SOURCE_FILES}" ${COVERALLS_UPLOAD})
endif ()

install(FILES ${SOURCE_FILES} DESTINATION include)
install(FILES ${MAN_FILES} DESTINATION share/man/man3)

add_custom_target(uninstall
        "${CMAKE_COMMAND}" -P "${CMAKE_MODULE_PATH}/uninstall.cmake"
        )

include(PackageConfig)

option(UPLOAD_DEB "Upload package to launchpad" OFF)

if (UNIX AND UPLOAD_DEB)
    include(DebConfig)
endif ()
add_executable(example example/demo.c)