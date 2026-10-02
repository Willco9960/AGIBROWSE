if(NOT AGI_BROWSE_WITH_CEF)
  add_executable(agi-browse-host src/main.cpp)
  target_compile_features(agi-browse-host PRIVATE cxx_std_17)
  return()
endif()

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Pinned CEF host requires Windows x64 with Visual Studio 2022 (MSVC v143)")
endif()
set(CEF_ROOT "" CACHE PATH "Verified pinned CEF distribution (tools/cef/bootstrap.ps1)")
if(NOT EXISTS "${CEF_ROOT}/agi-browse-pin.sha256")
  message(FATAL_ERROR "Run tools/cef/bootstrap.ps1 first; unverified CEF_ROOT is rejected")
endif()
file(READ "${CEF_ROOT}/agi-browse-pin.sha256" CEF_VERIFIED_HASH)
string(STRIP "${CEF_VERIFIED_HASH}" CEF_VERIFIED_HASH)
if(NOT CEF_VERIFIED_HASH STREQUAL "a211b8f10a99d1db49afd5b7d9fa7ab3222ebc6ea3026b31c3c9431e7d4a5ac2")
  message(FATAL_ERROR "CEF distribution does not match the task 003 pin")
endif()
file(READ "${CEF_ROOT}/include/cef_version.h" CEF_VERSION_HEADER)
if(NOT CEF_VERSION_HEADER MATCHES "a03e7146331fc5bd72784591e82df01e8007e17b")
  message(FATAL_ERROR "CEF source revision mismatch")
endif()
set(PROJECT_ARCH "x86_64")
set(USE_SANDBOX ON CACHE BOOL "Required CEF sandbox" FORCE)
set(USE_ATL OFF CACHE BOOL "Views host does not use ATL" FORCE)
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/cef-wrapper")
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# CEF M138+ owns the sandbox in bootstrap.exe. Never link a replacement launcher.
add_library(agi-browse-host SHARED src/cef_main.cpp)
SET_LIBRARY_TARGET_PROPERTIES(agi-browse-host)
target_compile_features(agi-browse-host PRIVATE cxx_std_20)
target_include_directories(agi-browse-host PRIVATE "${CEF_ROOT}")
target_link_libraries(agi-browse-host PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})
set_target_properties(agi-browse-host PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/browser/$<CONFIG>"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CEF_TARGET_OUT_DIR "${CMAKE_BINARY_DIR}/browser/$<CONFIG>")
SET_LPAC_ACLS(agi-browse-host)
COPY_FILES(agi-browse-host "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:agi-browse-host>")
COPY_FILES(agi-browse-host "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:agi-browse-host>")
add_custom_command(TARGET agi-browse-host POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BINARY_DIR}/bootstrap.exe" "$<TARGET_FILE_DIR:agi-browse-host>/agi-browse-host.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_ROOT}/LICENSE.txt" "$<TARGET_FILE_DIR:agi-browse-host>/CEF-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_ROOT}/README.txt" "$<TARGET_FILE_DIR:agi-browse-host>/CEF-README.txt"
  VERBATIM)
