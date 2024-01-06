set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "/home/ksenia/vsc/combos/simgrid-3.34")

find_package(SimGrid REQUIRED)

set(Boost_USE_STATIC_LIBS OFF) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF) 
find_package(Boost 1.45.0 COMPONENTS context) 

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS}) 


# find_package(SimGrid REQUIRED)
    include_directories(${SimGrid_INCLUDE_DIR})

    add_executable(boinc_sim_cpp boinc.cpp rand.cpp components/scheduler.cpp components/shared.cpp)
    target_link_libraries(boinc_sim_cpp ${SimGrid_LIBRARY} ${Boost_LIBRARIES})

endif()