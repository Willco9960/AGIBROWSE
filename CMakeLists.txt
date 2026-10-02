cmake_minimum_required(VERSION 3.20)
project(agi_browse LANGUAGES CXX)

option(AGI_BROWSE_WITH_CEF "Build the browser host against the pinned CEF SDK (task 003)" OFF)
if(AGI_BROWSE_WITH_CEF)
  message(FATAL_ERROR "CEF integration is owned by AIBROWESE-003; scaffold builds use -DAGI_BROWSE_WITH_CEF=OFF")
endif()

add_subdirectory(apps/browser)
add_subdirectory(apps/broker)
