/**
 * @file pyviamd.cpp
 * @brief Main Python bindings entry point for VIAMD
 * 
 * This file provides Python bindings for VIAMD's core functionality,
 * enabling integration with Python molecular analysis packages like
 * OpenMM and MDAnalysis.
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

// Forward declarations for binding functions
void bind_core(pybind11::module &m);
void bind_molecule(pybind11::module &m);
void bind_trajectory(pybind11::module &m);
void bind_openmm(pybind11::module &m);
void init_mdanalysis_bindings(pybind11::module &m);
void init_event_bindings(pybind11::module &m);

PYBIND11_MODULE(pyviamd, m) {
    m.doc() = "Python bindings for VIAMD - Visual Interactive Analysis of Molecular Dynamics";
    
    m.attr("__version__") = "0.1.0";
    
    // Bind core functionality
    bind_core(m);
    
    // Bind molecule functionality
    bind_molecule(m);
    
    // Bind trajectory functionality  
    bind_trajectory(m);
    
    // Bind OpenMM integration functionality
    bind_openmm(m);
    
    // Bind MDAnalysis integration functionality
    init_mdanalysis_bindings(m);
    
    // Bind event system functionality
    init_event_bindings(m);
}