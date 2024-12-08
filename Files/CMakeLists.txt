include(FetchContent)

include(ExternalProject)

# SimGrid
ExternalProject_Add(simgrid_external
  GIT_REPOSITORY    https://github.com/simgrid/simgrid.git
  GIT_TAG           v3.35
  CMAKE_ARGS        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/simgrid
  BUILD_ALWAYS      0
)
# Set up SimGrid include and lib directories
set(SIMGRID_INCLUDE_DIR ${CMAKE_BINARY_DIR}/simgrid/include)
set(SIMGRID_LIB_DIR ${CMAKE_BINARY_DIR}/simgrid/lib)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG master
)
FetchContent_GetProperties(yaml-cpp)

if(NOT yaml-cpp_POPULATED)
  message(STATUS "Fetching yaml-cpp...")
  FetchContent_Populate(yaml-cpp)
  add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR})
endif()

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
FetchContent_MakeAvailable(googletest)


set(Boost_USE_STATIC_LIBS OFF) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF) 
find_package(Boost 1.82.0 COMPONENTS context) 


if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS}) 

    include_directories("${PROJECT_SOURCE_DIR}/Files")
    set (CMAKE_CXX_STANDARD 20)
    link_directories(/usr/local/lib)

    

    # Todo: if possible, how can I split it
    add_executable(boinc_sim_cpp boinc.cpp components/scheduler.cpp
                   components/data_server.cpp components/shared.cpp
                   components/data_client_server.cpp client_side/data_client.cpp
                   client_side/fetch_work.cpp client_side/execute_task.cpp
                   tools/execution_state.cpp)
    target_link_libraries(boinc_sim_cpp ${SIMGRID_LIB_DIR}/libsimgrid.so ${Boost_LIBRARIES} yaml-cpp)

    # Add include directories
target_include_directories(boinc_sim_cpp PRIVATE ${SIMGRID_INCLUDE_DIR})

add_dependencies(boinc_sim_cpp simgrid_external)
endif()

add_executable(create_deployment create_deployment.cpp)
target_link_libraries(create_deployment yaml-cpp)

add_executable(create_platform create_platform.cpp)



enable_testing()

add_executable(
  main_tests
  client_side/data_client_test.cpp
  client_side/data_client.cpp
  components/shared.cpp
)
target_link_libraries(
  main_tests
  GTest::gtest_main
  yaml-cpp
  ${SIMGRID_LIB_DIR}/libsimgrid.so
)
target_include_directories(main_tests PRIVATE ${SIMGRID_INCLUDE_DIR})
add_dependencies(main_tests simgrid_external)


include(GoogleTest)
gtest_discover_tests(main_tests)