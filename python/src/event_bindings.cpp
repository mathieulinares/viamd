#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/chrono.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <event.h>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>

namespace py = pybind11;

namespace viamd {

// Python-friendly EventHandler wrapper
class PyEventHandler : public EventHandler {
private:
    std::function<void(const std::vector<Event>&)> py_callback;
    std::string handler_name;
    std::vector<EventType> subscribed_events;
    
public:
    PyEventHandler(const std::string& name, std::function<void(const std::vector<Event>&)> callback)
        : py_callback(callback), handler_name(name) {}
    
    void process_events(const Event* events, size_t num_events) override {
        if (!py_callback || num_events == 0) return;
        
        std::vector<Event> filtered_events;
        
        // Filter events if subscriptions are set
        if (!subscribed_events.empty()) {
            for (size_t i = 0; i < num_events; ++i) {
                for (EventType subscribed : subscribed_events) {
                    if (events[i].type == subscribed) {
                        filtered_events.push_back(events[i]);
                        break;
                    }
                }
            }
        } else {
            // No filtering, process all events
            filtered_events.assign(events, events + num_events);
        }
        
        if (!filtered_events.empty()) {
            try {
                py_callback(filtered_events);
            } catch (const std::exception& e) {
                // Log error but don't crash the system
                printf("Error in Python event handler '%s': %s\n", handler_name.c_str(), e.what());
            }
        }
    }
    
    void subscribe_to_events(const std::vector<EventType>& events) {
        subscribed_events = events;
    }
    
    void subscribe_to_event(EventType event) {
        subscribed_events.push_back(event);
    }
    
    void clear_subscriptions() {
        subscribed_events.clear();
    }
    
    const std::string& get_name() const { return handler_name; }
    const std::vector<EventType>& get_subscriptions() const { return subscribed_events; }
};

// Event manager for Python
class PyEventManager {
private:
    std::unordered_map<std::string, std::unique_ptr<PyEventHandler>> handlers;
    
public:
    // Register a Python event handler
    void register_handler(const std::string& name, 
                         std::function<void(const std::vector<Event>&)> callback) {
        auto handler = std::make_unique<PyEventHandler>(name, callback);
        event_system_register_handler(*handler);
        handlers[name] = std::move(handler);
    }
    
    // Subscribe handler to specific events
    void subscribe_handler_to_events(const std::string& name, const std::vector<EventType>& events) {
        auto it = handlers.find(name);
        if (it != handlers.end()) {
            it->second->subscribe_to_events(events);
        }
    }
    
    void subscribe_handler_to_event(const std::string& name, EventType event) {
        auto it = handlers.find(name);
        if (it != handlers.end()) {
            it->second->subscribe_to_event(event);
        }
    }
    
    // Send events to the system
    void enqueue_event(EventType type, EventPayloadType payload_type = EventPayloadType_Undefined, 
                      py::object payload = py::none(), uint64_t delay_in_ms = 0) {
        // For simplicity, we'll store Python objects in a static map and pass pointers
        // In a production system, you'd want more sophisticated payload handling
        void* payload_ptr = nullptr;
        if (!payload.is_none()) {
            // This is a simplified approach - in production you'd want better payload management
            static std::vector<py::object> payload_storage;
            payload_storage.push_back(payload);
            payload_ptr = &payload_storage.back();
        }
        
        event_system_enqueue_event(type, payload_type, payload_ptr, delay_in_ms);
    }
    
    void broadcast_event(EventType type, EventPayloadType payload_type = EventPayloadType_Undefined,
                        py::object payload = py::none()) {
        void* payload_ptr = nullptr;
        if (!payload.is_none()) {
            static std::vector<py::object> payload_storage;
            payload_storage.push_back(payload);
            payload_ptr = &payload_storage.back();
        }
        
        event_system_broadcast_event(type, payload_type, payload_ptr);
    }
    
    // Process event queue (call this in your main loop)
    void process_events() {
        event_system_process_event_queue();
    }
    
    // Get registered handlers
    std::vector<std::string> get_handler_names() const {
        std::vector<std::string> names;
        for (const auto& pair : handlers) {
            names.push_back(pair.first);
        }
        return names;
    }
    
    // Get handler subscriptions
    std::vector<EventType> get_handler_subscriptions(const std::string& name) const {
        auto it = handlers.find(name);
        if (it != handlers.end()) {
            return it->second->get_subscriptions();
        }
        return {};
    }
    
    // Remove handler
    void unregister_handler(const std::string& name) {
        handlers.erase(name);
    }
};

// Global event manager instance
static PyEventManager global_event_manager;

// Python-friendly event data structure
struct PyEvent {
    EventType type;
    EventPayloadType payload_type;
    uint64_t timestamp;
    py::object payload;
    
    PyEvent(const Event& event) 
        : type(event.type), payload_type(event.payload_type), timestamp(event.timestamp) {
        // Convert payload if it's a Python object
        if (event.payload) {
            try {
                payload = *static_cast<const py::object*>(event.payload);
            } catch (...) {
                payload = py::none();
            }
        } else {
            payload = py::none();
        }
    }
};

// Convert Event array to PyEvent vector
std::vector<PyEvent> convert_events(const std::vector<Event>& events) {
    std::vector<PyEvent> py_events;
    py_events.reserve(events.size());
    for (const auto& event : events) {
        py_events.emplace_back(event);
    }
    return py_events;
}

} // namespace viamd

// Convenience functions for Python (outside namespace)
void register_event_handler(const std::string& name, 
                           std::function<void(const std::vector<viamd::PyEvent>&)> callback) {
    auto cpp_callback = [callback](const std::vector<viamd::Event>& events) {
        auto py_events = viamd::convert_events(events);
        callback(py_events);
    };
    viamd::global_event_manager.register_handler(name, cpp_callback);
}

void subscribe_to_events(const std::string& handler_name, const std::vector<viamd::EventType>& events) {
    viamd::global_event_manager.subscribe_handler_to_events(handler_name, events);
}

void subscribe_to_event(const std::string& handler_name, viamd::EventType event) {
    viamd::global_event_manager.subscribe_handler_to_event(handler_name, event);
}

void send_event(viamd::EventType type, viamd::EventPayloadType payload_type = viamd::EventPayloadType_Undefined,
               py::object payload = py::none(), uint64_t delay_in_ms = 0) {
    viamd::global_event_manager.enqueue_event(type, payload_type, payload, delay_in_ms);
}

void broadcast_event(viamd::EventType type, viamd::EventPayloadType payload_type = viamd::EventPayloadType_Undefined,
                    py::object payload = py::none()) {
    viamd::global_event_manager.broadcast_event(type, payload_type, payload);
}

void process_event_queue() {
    viamd::global_event_manager.process_events();
}

std::vector<std::string> get_registered_handlers() {
    return viamd::global_event_manager.get_handler_names();
}

void unregister_handler(const std::string& name) {
    viamd::global_event_manager.unregister_handler(name);
}

// Event type and payload type constants for Python
void init_event_bindings(py::module& m) {
    // Add event type constants directly to module
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
    
    // Add event payload type constants directly to module
    m.attr("EventPayloadType_Undefined") = viamd::EventPayloadType_Undefined;
    m.attr("EventPayloadType_ApplicationState") = viamd::EventPayloadType_ApplicationState;
    m.attr("EventPayloadType_RepresentationInfo") = viamd::EventPayloadType_RepresentationInfo;
    m.attr("EventPayloadType_Representation") = viamd::EventPayloadType_Representation;
    m.attr("EventPayloadType_SerializationState") = viamd::EventPayloadType_SerializationState;
    m.attr("EventPayloadType_DeserializationState") = viamd::EventPayloadType_DeserializationState;
    m.attr("EventPayloadType_EvalElectronicStructure") = viamd::EventPayloadType_EvalElectronicStructure;
    m.attr("EventPayloadType_EvalAtomProperty") = viamd::EventPayloadType_EvalAtomProperty;
    
    // PyEvent class
    py::class_<viamd::PyEvent>(m, "Event")
        .def_readonly("type", &viamd::PyEvent::type)
        .def_readonly("payload_type", &viamd::PyEvent::payload_type)
        .def_readonly("timestamp", &viamd::PyEvent::timestamp)
        .def_readonly("payload", &viamd::PyEvent::payload)
        .def("__repr__", [](const viamd::PyEvent& e) {
            return "<Event type=" + std::to_string(e.type) + 
                   " payload_type=" + std::to_string(e.payload_type) + 
                   " timestamp=" + std::to_string(e.timestamp) + ">";
        });
    
    // Event system functions
    m.def("register_event_handler", &register_event_handler,
          "Register a Python event handler",
          py::arg("name"), py::arg("callback"));
    
    m.def("subscribe_to_events", &subscribe_to_events,
          "Subscribe handler to specific event types",
          py::arg("handler_name"), py::arg("events"));
    
    m.def("subscribe_to_event", &subscribe_to_event,
          "Subscribe handler to a specific event type",
          py::arg("handler_name"), py::arg("event"));
    
    m.def("send_event", &send_event,
          "Send an event to the event queue",
          py::arg("type"), py::arg("payload_type") = viamd::EventPayloadType_Undefined,
          py::arg("payload") = py::none(), py::arg("delay_in_ms") = 0);
    
    m.def("broadcast_event", &broadcast_event,
          "Immediately broadcast an event to all handlers",
          py::arg("type"), py::arg("payload_type") = viamd::EventPayloadType_Undefined,
          py::arg("payload") = py::none());
    
    m.def("process_event_queue", &process_event_queue,
          "Process the event queue (call this in your main loop)");
    
    m.def("get_registered_handlers", &get_registered_handlers,
          "Get list of registered handler names");
    
    m.def("unregister_handler", &unregister_handler,
          "Unregister an event handler",
          py::arg("name"));
}