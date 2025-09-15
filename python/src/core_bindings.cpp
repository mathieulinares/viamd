/**
 * @file core_bindings.cpp
 * @brief Core VIAMD functionality bindings
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <core/md_allocator.h>
#include <core/md_str.h>
#include <core/md_log.h>
#include <core/md_common.h>

namespace py = pybind11;

void bind_core(py::module &m) {
    // Create a submodule for core functionality
    auto core = m.def_submodule("core", "Core VIAMD functionality");
    
    // Bind logging functionality
    core.def("log_info", [](const std::string& msg) {
        MD_LOG_INFO("%s", msg.c_str());
    }, "Log an info message");
    
    core.def("log_debug", [](const std::string& msg) {
        MD_LOG_DEBUG("%s", msg.c_str());
    }, "Log a debug message");
    
    core.def("log_error", [](const std::string& msg) {
        MD_LOG_ERROR("%s", msg.c_str());
    }, "Log an error message");
    
    // Bind string utilities
    core.def("str_from_cstr", [](const char* cstr) -> std::string {
        if (!cstr) return "";
        return std::string(cstr);
    }, "Convert C string to Python string");
    
    // Bind basic allocator access (simplified)
    core.def("get_temp_allocator_max_size", []() -> size_t {
        return md_temp_allocator_max_allocation_size();
    }, "Get maximum size for temporary allocator");
    
    core.def("get_temp_allocator_position", []() -> size_t {
        return md_temp_get_pos();
    }, "Get current position in temporary allocator");
    
    // Bind version information
    core.def("get_version", []() -> std::string {
        return "VIAMD v0.1.15 with Python bindings";
    }, "Get VIAMD version information");
}