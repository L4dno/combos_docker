#!/bin/bash

# Function to check if a directory contains Boost headers
function check_boost_headers() {
    local boost_headers_dir="$1"
    if [ -d "$boost_headers_dir/boost" ]; then
        echo "Boost headers found in: $boost_headers_dir"
        return 0  # Boost headers found
    else
        echo "Boost headers not found in: $boost_headers_dir"
        return 1  # Boost headers not found
    fi
}

# Function to check if Boost libraries are available
function check_boost_libraries() {
    local boost_libraries="$1"
    if [ -n "$(find $boost_libraries -name 'libboost_*')" ]; then
        echo "Boost libraries found in: $boost_libraries"
        return 0  # Boost libraries found
    else
        echo "Boost libraries not found in: $boost_libraries"
        return 1  # Boost libraries not found
    fi
}


function find_or_install_boost() {
    # Check Boost headers in common system locations
    if check_boost_headers "/usr/include" || check_boost_headers "/opt/boost/include"; then
        return 0  # Boost headers found
    fi

    # Check Boost libraries in common system locations
    if check_boost_libraries "/usr/lib" || check_boost_libraries "/usr/lib64" || check_boost_libraries "/opt/boost/lib"; then
        return 0  # Boost libraries found
    fi

    # If Boost is not found in standard locations, it may be installed elsewhere
    echo "Boost not found. Installing Boost."
    wget https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.bz2
    tar --bzip2 -xf boost_1_82_0.tar.bz2
    cd boost_1_82_0 && ./bootstrap.sh --prefix=/opt/boost && sudo ./b2 install --with-context && cd ..
}


function install_simgrid() {
    git clone https://github.com/simgrid/simgrid.git simgrid
    cd simgrid
    cmake -DCMAKE_INSTALL_PREFIX=/opt/simgrid .
    make 
    echo "Installing SimGrid"
    sudo make install
    cd ..
}

function build_combos() {
    # prepare ComBoS to work
    mkdir build
    cd build && cmake .. && cd ..
}

find_or_install_boost
install_simgrid
build_combos
