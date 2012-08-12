cmake_minimum_required(VERSION 2.8.7)
project(soakfs)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED OFF)
find_package(Boost 1.50.0 REQUIRED system thread regex filesystem)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
set(CMAKE_VERBOSE_MAKEFILE false)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions(-DBOOST_NETWORK_DEBUG)
  add_definitions(-DJSON_DEBUG)
endif()
add_definitions(-DBOOST_NETWORK_ENABLE_HTTPS)
add_definitions(-DJSON_ISO_STRICT)
add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-DFUSE_USE_VERSION=26)


link_directories(${CPPNETLIB_ROOT}/build/libs/network/src)

set(soakfs_src
  main.cpp
)

aux_source_directory(${LIBJSON_ROOT}/_internal/Source libjson_src)
include_directories(${LIBJSON_ROOT}/_internal/Source)

# base-n
include_directories(${BASEN_ROOT}/include)

# libjson
include_directories(${LIBJSON_ROOT})

add_executable(soakfs ${soakfs_src} ${libjson_src})
add_custom_target(run
  COMMAND soakfs
  DEPENDS soakfs
  WORKING_DIRECTORY ${CMAKE_PROJECT_DIR}
)

# fuse
target_link_libraries(soakfs 
  fuse
)

if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  target_link_libraries(soakfs
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
  )
endif(Boost_FOUND)

# cpp-netlib and dependencies
include_directories(${CPPNETLIB_ROOT})
link_directories(${CPPNETLIB_ROOT}/build/libs/network/src)
target_link_libraries(soakfs 
  cppnetlib-client-connections
  cppnetlib-uri
)

if(OPENSSL_FOUND)
  include_directories(${OPENSSL_INCLUDE_DIR})
  target_link_libraries(soakfs
    ${OPENSSL_LIBRARIES}
  )
endif(OPENSSL_FOUND)

set(CMAKE_CXX_FLAGS "-std=c++0x")

