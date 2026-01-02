cmake_minimum_required(VERSION 3.15)

# Fix up a macOS .app bundle by copying non-Qt dylibs into
# Contents/Frameworks and rewriting install names to use @rpath/@loader_path.
#
# Usage:
#   cmake -DAPP_BUNDLE=/path/to/kge.app -P cmake/fix_bundle.cmake

if (NOT APP_BUNDLE)
    message(FATAL_ERROR "APP_BUNDLE not set. Invoke with -DAPP_BUNDLE=/path/to/App.app")
endif ()

get_filename_component(APP_DIR "${APP_BUNDLE}" ABSOLUTE)
set(EXECUTABLE "${APP_DIR}/Contents/MacOS/kge")

if (NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found at: ${EXECUTABLE}")
endif ()

include(BundleUtilities)

# Directories to search when resolving prerequisites. We include Homebrew so that
# if any deps are currently resolved from there, fixup_bundle will copy them into
# the bundle and rewrite install names to be self-contained.
set(DIRS
        "/usr/local/lib"
        "/opt/homebrew/lib"
        "/opt/homebrew/opt"
)

# Note: We pass empty plugin list so fixup_bundle scans the executable and all
# libs it references recursively. Qt frameworks already live in the bundle after
# macdeployqt; this step is primarily for non-Qt dylibs (glib, icu, pcre2, zstd,
# dbus, etc.).
# fixup_bundle often fails if copied libraries are read-only.
# We also try to use the system install_name_tool and otool to avoid issues with Anaconda's version.
# Note: BundleUtilities uses find_program(gp_otool "otool") internally, so we might need to set it differently.
set(gp_otool "/usr/bin/otool")
set(CMAKE_INSTALL_NAME_TOOL "/usr/bin/install_name_tool")
set(CMAKE_OTOOL "/usr/bin/otool")
set(ENV{PATH} "/usr/bin:/bin:/usr/sbin:/sbin")

execute_process(COMMAND chmod -R u+w "${APP_DIR}/Contents/Frameworks")

fixup_bundle("${APP_DIR}" "" "${DIRS}")

# On Apple Silicon (and modern macOS in general), modifications by fixup_bundle
# invalidate code signatures. We must re-sign the bundle (at least ad-hoc)
# for it to be allowed to run.
# We sign deep, but sometimes explicit signing of components is more reliable.
message(STATUS "Re-signing ${APP_DIR} after fixup...")

# 1. Sign dylibs in Frameworks
file(GLOB_RECURSE DYLIBS "${APP_DIR}/Contents/Frameworks/*.dylib")
foreach (DYLIB ${DYLIBS})
    message(STATUS "Signing ${DYLIB}...")
    execute_process(COMMAND /usr/bin/codesign --force --sign - "${DYLIB}")
endforeach ()

# 2. Sign nested executables
message(STATUS "Signing nested kte...")
execute_process(COMMAND /usr/bin/codesign --force --sign - "${APP_DIR}/Contents/MacOS/kte")

# 3. Sign the main executable explicitly
message(STATUS "Signing main kge...")
execute_process(COMMAND /usr/bin/codesign --force --sign - "${APP_DIR}/Contents/MacOS/kge")

# 4. Sign the main bundle
execute_process(
        COMMAND /usr/bin/codesign --force --deep --sign - "${APP_DIR}"
        RESULT_VARIABLE CODESIGN_RESULT
)

if (NOT CODESIGN_RESULT EQUAL 0)
    message(FATAL_ERROR "Codesign failed with error: ${CODESIGN_RESULT}")
endif ()

message(STATUS "fix_bundle.cmake completed for ${APP_DIR}")
