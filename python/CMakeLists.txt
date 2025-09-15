# Python bindings for VIAMD
cmake_minimum_required(VERSION 3.20)

# Find Python and pybind11
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 REQUIRED)

# Create the Python module
pybind11_add_module(pyviamd 
    src/pyviamd.cpp
    src/core_bindings.cpp
    src/molecule_bindings.cpp
    src/trajectory_bindings.cpp
)

# Set properties for the Python module
target_compile_features(pyviamd PRIVATE cxx_std_20)

# Include directories
target_include_directories(pyviamd PRIVATE
    ../src
    ../ext/mdlib/src
    ../ext/mdlib/include
    ../gen
)

# Set Position Independent Code for shared library compatibility
set_target_properties(pyviamd PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Link against VIAMD's libraries
target_link_libraries(pyviamd PRIVATE
    mdlib
)

# Copy VIAMD compile definitions and flags
target_compile_definitions(pyviamd PRIVATE
    ${MD_DEFINES}
    PYBIND11_DETAILED_ERROR_MESSAGES
)

# Copy compilation flags and add -fPIC
target_compile_options(pyviamd PRIVATE ${MD_FLAGS} -fPIC)

# Set the output name to match Python import expectations
set_target_properties(pyviamd PROPERTIES
    OUTPUT_NAME "pyviamd"
    PREFIX ""
)

# Installation
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Install the Python module
    install(TARGETS pyviamd
        LIBRARY DESTINATION python
        RUNTIME DESTINATION python
    )
endif()