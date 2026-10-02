cmake_minimum_required(VERSION 3.21)
project(agi_browse LANGUAGES CXX)

option(AGI_BROWSE_WITH_CEF "Build the browser host against the pinned CEF SDK (task 003)" OFF)
add_subdirectory(apps/browser)
add_subdirectory(apps/broker)
