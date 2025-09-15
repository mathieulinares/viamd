"""
VIAMD Event System Integration

This module provides Python integration with VIAMD's event system, enabling
custom Python components to participate in VIAMD's event-driven architecture.

Example Usage:
    import pyviamd
    from pyviamd.event import EventManager, EventType
    
    # Create event manager
    event_mgr = EventManager()
    
    # Define event handler
    def on_topology_change(events):
        for event in events:
            print(f"Topology changed: {event.type}")
    
    # Register handler and subscribe to events
    event_mgr.register_handler("topology_listener", on_topology_change)
    event_mgr.subscribe_to_event("topology_listener", EventType.ViamdTopologyInit)
    
    # Send custom events
    event_mgr.send_event(EventType.ViamdFrameTick, payload={"frame": 42})
"""

import pyviamd
from typing import List, Callable, Any, Optional, Dict
import time

# Re-export types for convenience - create wrapper classes for cleaner API
class EventType:
    """Event type constants for VIAMD events."""
    @property
    def ViamdInitialize(self):
        return pyviamd.EventType_ViamdInitialize
    
    @property
    def ViamdShutdown(self):
        return pyviamd.EventType_ViamdShutdown
    
    @property
    def ViamdFrameTick(self):
        return pyviamd.EventType_ViamdFrameTick
    
    @property
    def ViamdRenderOpaque(self):
        return pyviamd.EventType_ViamdRenderOpaque
    
    @property
    def ViamdRenderTransparent(self):
        return pyviamd.EventType_ViamdRenderTransparent
    
    @property
    def ViamdWindowDrawMenu(self):
        return pyviamd.EventType_ViamdWindowDrawMenu
    
    @property
    def ViamdSerialize(self):
        return pyviamd.EventType_ViamdSerialize
    
    @property
    def ViamdDeserialize(self):
        return pyviamd.EventType_ViamdDeserialize
    
    @property
    def ViamdTopologyInit(self):
        return pyviamd.EventType_ViamdTopologyInit
    
    @property
    def ViamdTopologyFree(self):
        return pyviamd.EventType_ViamdTopologyFree
    
    @property
    def ViamdTrajectoryInit(self):
        return pyviamd.EventType_ViamdTrajectoryInit
    
    @property
    def ViamdTrajectoryFree(self):
        return pyviamd.EventType_ViamdTrajectoryFree
    
    @property
    def ViamdHoverMaskChanged(self):
        return pyviamd.EventType_ViamdHoverMaskChanged
    
    @property
    def ViamdSelectionMaskChanged(self):
        return pyviamd.EventType_ViamdSelectionMaskChanged
    
    @property
    def RepresentationInfoFill(self):
        return pyviamd.EventType_RepresentationInfoFill
    
    @property
    def RepresentationEvalElectronicStructure(self):
        return pyviamd.EventType_RepresentationEvalElectronicStructure
    
    @property
    def RepresentationEvalAtomProperty(self):
        return pyviamd.EventType_RepresentationEvalAtomProperty

class EventPayloadType:
    """Event payload type constants for VIAMD events."""
    @property
    def Undefined(self):
        return pyviamd.EventPayloadType_Undefined
    
    @property
    def ApplicationState(self):
        return pyviamd.EventPayloadType_ApplicationState
    
    @property
    def RepresentationInfo(self):
        return pyviamd.EventPayloadType_RepresentationInfo
    
    @property
    def Representation(self):
        return pyviamd.EventPayloadType_Representation
    
    @property
    def SerializationState(self):
        return pyviamd.EventPayloadType_SerializationState
    
    @property
    def DeserializationState(self):
        return pyviamd.EventPayloadType_DeserializationState
    
    @property
    def EvalElectronicStructure(self):
        return pyviamd.EventPayloadType_EvalElectronicStructure
    
    @property
    def EvalAtomProperty(self):
        return pyviamd.EventPayloadType_EvalAtomProperty

# Create instances for easy access
EventType = EventType()
EventPayloadType = EventPayloadType()
Event = pyviamd.Event

class EventManager:
    """
    High-level Python interface to VIAMD's event system.
    
    This class provides a convenient way to manage event handlers,
    subscriptions, and event processing in Python.
    """
    
    def __init__(self):
        self._handlers: Dict[str, Callable] = {}
        self._subscriptions: Dict[str, List[EventType]] = {}
    
    def register_handler(self, name: str, callback: Callable[[List[Event]], None]) -> None:
        """
        Register a Python event handler.
        
        Args:
            name: Unique name for the handler
            callback: Function that takes a list of Event objects
        """
        if name in self._handlers:
            raise ValueError(f"Handler '{name}' already registered")
        
        self._handlers[name] = callback
        pyviamd.register_event_handler(name, callback)
    
    def subscribe_to_event(self, handler_name: str, event_type: EventType) -> None:
        """
        Subscribe a handler to a specific event type.
        
        Args:
            handler_name: Name of the registered handler
            event_type: Type of event to subscribe to
        """
        if handler_name not in self._handlers:
            raise ValueError(f"Handler '{handler_name}' not registered")
        
        if handler_name not in self._subscriptions:
            self._subscriptions[handler_name] = []
        
        if event_type not in self._subscriptions[handler_name]:
            self._subscriptions[handler_name].append(event_type)
            pyviamd.subscribe_to_event(handler_name, event_type)
    
    def subscribe_to_events(self, handler_name: str, event_types: List[EventType]) -> None:
        """
        Subscribe a handler to multiple event types.
        
        Args:
            handler_name: Name of the registered handler
            event_types: List of event types to subscribe to
        """
        for event_type in event_types:
            self.subscribe_to_event(handler_name, event_type)
    
    def send_event(self, event_type: EventType, 
                   payload_type: EventPayloadType = EventPayloadType.Undefined,
                   payload: Any = None, 
                   delay_ms: int = 0) -> None:
        """
        Send an event to the event queue.
        
        Args:
            event_type: Type of event to send
            payload_type: Type of the payload (optional)
            payload: Payload data (optional)
            delay_ms: Delay in milliseconds before processing (optional)
        """
        pyviamd.send_event(event_type, payload_type, payload, delay_ms)
    
    def broadcast_event(self, event_type: EventType,
                       payload_type: EventPayloadType = EventPayloadType.Undefined,
                       payload: Any = None) -> None:
        """
        Immediately broadcast an event to all handlers.
        
        Args:
            event_type: Type of event to broadcast
            payload_type: Type of the payload (optional)
            payload: Payload data (optional)
        """
        pyviamd.broadcast_event(event_type, payload_type, payload)
    
    def process_events(self) -> None:
        """Process the event queue. Call this in your main loop."""
        pyviamd.process_event_queue()
    
    def unregister_handler(self, name: str) -> None:
        """
        Unregister an event handler.
        
        Args:
            name: Name of the handler to unregister
        """
        if name in self._handlers:
            del self._handlers[name]
        if name in self._subscriptions:
            del self._subscriptions[name]
        pyviamd.unregister_handler(name)
    
    def get_registered_handlers(self) -> List[str]:
        """Get list of registered handler names."""
        return list(self._handlers.keys())
    
    def get_handler_subscriptions(self, name: str) -> List[EventType]:
        """
        Get event subscriptions for a handler.
        
        Args:
            name: Name of the handler
            
        Returns:
            List of subscribed event types
        """
        return self._subscriptions.get(name, [])


class EventLogger:
    """
    Utility class for logging VIAMD events.
    
    This can be useful for debugging and monitoring system behavior.
    """
    
    def __init__(self, log_file: Optional[str] = None):
        self.log_file = log_file
        self.event_count = 0
        self.event_manager = EventManager()
        
        # Register as event handler
        self.event_manager.register_handler("event_logger", self._log_events)
        
        # Subscribe to all major events by default
        self.subscribe_to_all_events()
    
    def subscribe_to_all_events(self):
        """Subscribe to all major VIAMD events."""
        major_events = [
            EventType.ViamdInitialize,
            EventType.ViamdShutdown,
            EventType.ViamdFrameTick,
            EventType.ViamdTopologyInit,
            EventType.ViamdTopologyFree,
            EventType.ViamdTrajectoryInit,
            EventType.ViamdTrajectoryFree,
            EventType.ViamdHoverMaskChanged,
            EventType.ViamdSelectionMaskChanged,
        ]
        
        for event_type in major_events:
            self.event_manager.subscribe_to_event("event_logger", event_type)
    
    def _log_events(self, events: List[Event]) -> None:
        """Internal method to log events."""
        for event in events:
            self.event_count += 1
            log_msg = f"[{self.event_count:06d}] Event: type={event.type}, payload_type={event.payload_type}, timestamp={event.timestamp}"
            
            if self.log_file:
                with open(self.log_file, 'a') as f:
                    f.write(log_msg + '\n')
            else:
                print(log_msg)
    
    def get_event_count(self) -> int:
        """Get total number of events logged."""
        return self.event_count


class MolecularEventHandler:
    """
    Specialized event handler for molecular events.
    
    This class provides convenient callbacks for common molecular
    dynamics events like topology changes, frame updates, etc.
    """
    
    def __init__(self):
        self.event_manager = EventManager()
        self.event_manager.register_handler("molecular_handler", self._handle_events)
        
        # Callbacks for specific events
        self.on_topology_init: Optional[Callable[[Event], None]] = None
        self.on_topology_free: Optional[Callable[[Event], None]] = None
        self.on_trajectory_init: Optional[Callable[[Event], None]] = None
        self.on_trajectory_free: Optional[Callable[[Event], None]] = None
        self.on_frame_tick: Optional[Callable[[Event], None]] = None
        self.on_selection_changed: Optional[Callable[[Event], None]] = None
        
        # Subscribe to molecular events
        molecular_events = [
            EventType.ViamdTopologyInit,
            EventType.ViamdTopologyFree,
            EventType.ViamdTrajectoryInit,
            EventType.ViamdTrajectoryFree,
            EventType.ViamdFrameTick,
            EventType.ViamdSelectionMaskChanged,
        ]
        
        for event_type in molecular_events:
            self.event_manager.subscribe_to_event("molecular_handler", event_type)
    
    def _handle_events(self, events: List[Event]) -> None:
        """Internal event dispatcher."""
        for event in events:
            if event.type == EventType.ViamdTopologyInit and self.on_topology_init:
                self.on_topology_init(event)
            elif event.type == EventType.ViamdTopologyFree and self.on_topology_free:
                self.on_topology_free(event)
            elif event.type == EventType.ViamdTrajectoryInit and self.on_trajectory_init:
                self.on_trajectory_init(event)
            elif event.type == EventType.ViamdTrajectoryFree and self.on_trajectory_free:
                self.on_trajectory_free(event)
            elif event.type == EventType.ViamdFrameTick and self.on_frame_tick:
                self.on_frame_tick(event)
            elif event.type == EventType.ViamdSelectionMaskChanged and self.on_selection_changed:
                self.on_selection_changed(event)


# Convenience functions for common use cases
def create_event_manager() -> EventManager:
    """Create a new event manager instance."""
    return EventManager()

def create_event_logger(log_file: Optional[str] = None) -> EventLogger:
    """Create a new event logger instance."""
    return EventLogger(log_file)

def create_molecular_handler() -> MolecularEventHandler:
    """Create a new molecular event handler instance."""
    return MolecularEventHandler()

def send_custom_event(event_type: int, payload: Any = None) -> None:
    """
    Send a custom event with a user-defined event type.
    
    Args:
        event_type: Custom event type (should be a unique integer)
        payload: Optional payload data
    """
    pyviamd.send_event(event_type, EventPayloadType.Undefined, payload)

def process_all_events() -> None:
    """Process all pending events in the queue."""
    pyviamd.process_event_queue()