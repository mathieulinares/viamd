#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <event.h>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>

namespace py = pybind11;

// Simple event system implementation for Python bindings
namespace py_event_system {

struct PyEvent {
    uint32_t type;
    uint32_t payload_type;
    uint64_t timestamp;
    py::object payload;
    
    PyEvent(uint32_t t, uint32_t pt, uint64_t ts, py::object p = py::none())
        : type(t), payload_type(pt), timestamp(ts), payload(p) {}
};

class PyEventHandler {
private:
    std::string name;
    std::function<void(const std::vector<PyEvent>&)> callback;
    std::vector<uint32_t> subscribed_events;
    
public:
    PyEventHandler(const std::string& n, std::function<void(const std::vector<PyEvent>&)> cb)
        : name(n), callback(cb) {}
    
    void subscribe_to_event(uint32_t event_type) {
        subscribed_events.push_back(event_type);
    }
    
    void subscribe_to_events(const std::vector<uint32_t>& event_types) {
        subscribed_events.insert(subscribed_events.end(), event_types.begin(), event_types.end());
    }
    
    void process_events(const std::vector<PyEvent>& events) {
        if (!callback) return;
        
        std::vector<PyEvent> filtered_events;
        for (const auto& event : events) {
            if (subscribed_events.empty() || 
                std::find(subscribed_events.begin(), subscribed_events.end(), event.type) != subscribed_events.end()) {
                filtered_events.push_back(event);
            }
        }
        
        if (!filtered_events.empty()) {
            try {
                callback(filtered_events);
            } catch (const std::exception& e) {
                printf("Error in Python event handler '%s': %s\n", name.c_str(), e.what());
            }
        }
    }
    
    const std::string& get_name() const { return name; }
    const std::vector<uint32_t>& get_subscriptions() const { return subscribed_events; }
};

class PyEventManager {
private:
    std::unordered_map<std::string, std::unique_ptr<PyEventHandler>> handlers;
    std::vector<PyEvent> event_queue;
    
public:
    void register_handler(const std::string& name, 
                         std::function<void(const std::vector<PyEvent>&)> callback) {
        handlers[name] = std::make_unique<PyEventHandler>(name, callback);
    }
    
    void subscribe_to_event(const std::string& handler_name, uint32_t event_type) {
        auto it = handlers.find(handler_name);
        if (it != handlers.end()) {
            it->second->subscribe_to_event(event_type);
        }
    }
    
    void subscribe_to_events(const std::string& handler_name, const std::vector<uint32_t>& event_types) {
        auto it = handlers.find(handler_name);
        if (it != handlers.end()) {
            it->second->subscribe_to_events(event_types);
        }
    }
    
    void send_event(uint32_t type, uint32_t payload_type = 0, py::object payload = py::none()) {
        event_queue.emplace_back(type, payload_type, 0, payload);
    }
    
    void process_events() {
        if (event_queue.empty()) return;
        
        // Process events for all handlers
        for (auto& handler_pair : handlers) {
            handler_pair.second->process_events(event_queue);
        }
        
        event_queue.clear();
    }
    
    std::vector<std::string> get_registered_handlers() const {
        std::vector<std::string> names;
        for (const auto& pair : handlers) {
            names.push_back(pair.first);
        }
        return names;
    }
    
    void unregister_handler(const std::string& name) {
        handlers.erase(name);
    }
};

static PyEventManager global_event_manager;

} // namespace py_event_system

// Python binding functions
void py_register_event_handler(const std::string& name, 
                              std::function<void(const std::vector<py_event_system::PyEvent>&)> callback) {
    py_event_system::global_event_manager.register_handler(name, callback);
}

void py_subscribe_to_event(const std::string& handler_name, uint32_t event_type) {
    py_event_system::global_event_manager.subscribe_to_event(handler_name, event_type);
}

void py_subscribe_to_events(const std::string& handler_name, const std::vector<uint32_t>& event_types) {
    py_event_system::global_event_manager.subscribe_to_events(handler_name, event_types);
}

void py_send_event(uint32_t type, uint32_t payload_type = 0, py::object payload = py::none()) {
    py_event_system::global_event_manager.send_event(type, payload_type, payload);
}

void py_process_event_queue() {
    py_event_system::global_event_manager.process_events();
}

std::vector<std::string> py_get_registered_handlers() {
    return py_event_system::global_event_manager.get_registered_handlers();
}

void py_unregister_handler(const std::string& name) {
    py_event_system::global_event_manager.unregister_handler(name);
}

void init_event_bindings(py::module& m) {
    // Event type constants
    m.attr("EventType_ViamdInitialize") = viamd::EventType_ViamdInitialize;
    m.attr("EventType_ViamdShutdown") = viamd::EventType_ViamdShutdown;
    m.attr("EventType_ViamdFrameTick") = viamd::EventType_ViamdFrameTick;
    m.attr("EventType_ViamdRenderOpaque") = viamd::EventType_ViamdRenderOpaque;
    m.attr("EventType_ViamdRenderTransparent") = viamd::EventType_ViamdRenderTransparent;
    m.attr("EventType_ViamdWindowDrawMenu") = viamd::EventType_ViamdWindowDrawMenu;
    m.attr("EventType_ViamdSerialize") = viamd::EventType_ViamdSerialize;
    m.attr("EventType_ViamdDeserialize") = viamd::EventType_ViamdDeserialize;
    m.attr("EventType_ViamdTopologyInit") = viamd::EventType_ViamdTopologyInit;
    m.attr("EventType_ViamdTopologyFree") = viamd::EventType_ViamdTopologyFree;
    m.attr("EventType_ViamdTrajectoryInit") = viamd::EventType_ViamdTrajectoryInit;
    m.attr("EventType_ViamdTrajectoryFree") = viamd::EventType_ViamdTrajectoryFree;
    m.attr("EventType_ViamdHoverMaskChanged") = viamd::EventType_ViamdHoverMaskChanged;
    m.attr("EventType_ViamdSelectionMaskChanged") = viamd::EventType_ViamdSelectionMaskChanged;
    m.attr("EventType_RepresentationInfoFill") = viamd::EventType_RepresentationInfoFill;
    m.attr("EventType_RepresentationEvalElectronicStructure") = viamd::EventType_RepresentationEvalElectronicStructure;
    m.attr("EventType_RepresentationEvalAtomProperty") = viamd::EventType_RepresentationEvalAtomProperty;
    
    // Event payload type constants
    m.attr("EventPayloadType_Undefined") = viamd::EventPayloadType_Undefined;
    m.attr("EventPayloadType_ApplicationState") = viamd::EventPayloadType_ApplicationState;
    m.attr("EventPayloadType_RepresentationInfo") = viamd::EventPayloadType_RepresentationInfo;
    m.attr("EventPayloadType_Representation") = viamd::EventPayloadType_Representation;
    m.attr("EventPayloadType_SerializationState") = viamd::EventPayloadType_SerializationState;
    m.attr("EventPayloadType_DeserializationState") = viamd::EventPayloadType_DeserializationState;
    m.attr("EventPayloadType_EvalElectronicStructure") = viamd::EventPayloadType_EvalElectronicStructure;
    m.attr("EventPayloadType_EvalAtomProperty") = viamd::EventPayloadType_EvalAtomProperty;
    
    // PyEvent class
    py::class_<py_event_system::PyEvent>(m, "Event")
        .def(py::init<uint32_t, uint32_t, uint64_t, py::object>(), 
             py::arg("type"), py::arg("payload_type") = 0, py::arg("timestamp") = 0, py::arg("payload") = py::none())
        .def_readonly("type", &py_event_system::PyEvent::type)
        .def_readonly("payload_type", &py_event_system::PyEvent::payload_type)
        .def_readonly("timestamp", &py_event_system::PyEvent::timestamp)
        .def_readonly("payload", &py_event_system::PyEvent::payload)
        .def("__repr__", [](const py_event_system::PyEvent& e) {
            return "<Event type=" + std::to_string(e.type) + 
                   " payload_type=" + std::to_string(e.payload_type) + 
                   " timestamp=" + std::to_string(e.timestamp) + ">";
        });
    
    // Event system functions
    m.def("register_event_handler", &py_register_event_handler,
          "Register a Python event handler",
          py::arg("name"), py::arg("callback"));
    
    m.def("subscribe_to_events", &py_subscribe_to_events,
          "Subscribe handler to specific event types",
          py::arg("handler_name"), py::arg("events"));
    
    m.def("subscribe_to_event", &py_subscribe_to_event,
          "Subscribe handler to a specific event type",
          py::arg("handler_name"), py::arg("event"));
    
    m.def("send_event", &py_send_event,
          "Send an event to the event queue",
          py::arg("type"), py::arg("payload_type") = 0,
          py::arg("payload") = py::none());
    
    m.def("process_event_queue", &py_process_event_queue,
          "Process the event queue");
    
    m.def("get_registered_handlers", &py_get_registered_handlers,
          "Get list of registered handler names");
    
    m.def("unregister_handler", &py_unregister_handler,
          "Unregister an event handler",
          py::arg("name"));
}