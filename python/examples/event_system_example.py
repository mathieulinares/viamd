#!/usr/bin/env python3
"""
VIAMD Event System Example

This example demonstrates how to use VIAMD's event system from Python
to create custom components that respond to molecular dynamics events.

Features demonstrated:
- Registering Python event handlers
- Subscribing to specific event types
- Sending custom events
- Event logging and monitoring
- Molecular event handling
"""

import sys
import time
import pyviamd
from pyviamd.event import EventManager, EventLogger, MolecularEventHandler, EventType, EventPayloadType

def basic_event_example():
    """Demonstrate basic event system usage."""
    print("=== Basic Event System Example ===")
    
    # Create event manager
    event_mgr = EventManager()
    
    # Define a simple event handler
    def my_event_handler(events):
        for event in events:
            print(f"Received event: type={event.type}, timestamp={event.timestamp}")
            if hasattr(event, 'payload') and event.payload is not None:
                print(f"  Payload: {event.payload}")
    
    # Register the handler
    event_mgr.register_handler("my_handler", my_event_handler)
    
    # Subscribe to frame tick events
    event_mgr.subscribe_to_event("my_handler", EventType.ViamdFrameTick)
    
    # Send some test events
    print("\nSending test events...")
    event_mgr.send_event(EventType.ViamdFrameTick, payload={"frame": 1, "time": 0.1})
    event_mgr.send_event(EventType.ViamdFrameTick, payload={"frame": 2, "time": 0.2})
    
    # Process the events
    print("\nProcessing events...")
    event_mgr.process_events()
    
    print("Basic example completed.\n")

def molecular_event_example():
    """Demonstrate molecular event handling."""
    print("=== Molecular Event Handling Example ===")
    
    # Create molecular event handler
    mol_handler = MolecularEventHandler()
    
    # Define callbacks for molecular events
    def on_topology_loaded(event):
        print(f"Topology loaded at timestamp {event.timestamp}")
        if hasattr(event, 'payload') and event.payload:
            print(f"  Topology info: {event.payload}")
    
    def on_frame_update(event):
        print(f"Frame updated at timestamp {event.timestamp}")
        if hasattr(event, 'payload') and event.payload:
            frame_data = event.payload
            print(f"  Frame: {frame_data.get('frame', 'unknown')}")
    
    def on_selection_changed(event):
        print(f"Selection changed at timestamp {event.timestamp}")
        if hasattr(event, 'payload') and event.payload:
            selection_data = event.payload
            print(f"  Selected atoms: {selection_data.get('atoms', [])}")
    
    # Assign callbacks
    mol_handler.on_topology_init = on_topology_loaded
    mol_handler.on_frame_tick = on_frame_update
    mol_handler.on_selection_changed = on_selection_changed
    
    # Send some molecular events
    print("\nSimulating molecular events...")
    
    # Simulate topology loading
    mol_handler.event_manager.send_event(
        EventType.ViamdTopologyInit,
        EventPayloadType.RepresentationInfo,
        {"atoms": 1000, "residues": 100, "chains": 1}
    )
    
    # Simulate frame updates
    for frame in range(3):
        mol_handler.event_manager.send_event(
            EventType.ViamdFrameTick,
            payload={"frame": frame, "time": frame * 0.1}
        )
    
    # Simulate selection change
    mol_handler.event_manager.send_event(
        EventType.ViamdSelectionMaskChanged,
        payload={"atoms": [1, 5, 10, 15], "selection_type": "manual"}
    )
    
    # Process all events
    print("\nProcessing molecular events...")
    mol_handler.event_manager.process_events()
    
    print("Molecular example completed.\n")

def event_logging_example():
    """Demonstrate event logging and monitoring."""
    print("=== Event Logging Example ===")
    
    # Create event logger
    logger = EventLogger()
    
    # Create additional event manager for sending events
    event_mgr = EventManager()
    
    # Send various types of events
    print("Sending various events for logging...")
    
    events_to_send = [
        (EventType.ViamdInitialize, {"version": "1.0", "mode": "python"}),
        (EventType.ViamdTopologyInit, {"file": "test.pdb", "atoms": 500}),
        (EventType.ViamdTrajectoryInit, {"file": "test.xtc", "frames": 100}),
        (EventType.ViamdFrameTick, {"frame": 0}),
        (EventType.ViamdFrameTick, {"frame": 1}),
        (EventType.ViamdSelectionMaskChanged, {"atoms": [1, 2, 3]}),
        (EventType.ViamdShutdown, {"exit_code": 0}),
    ]
    
    for event_type, payload in events_to_send:
        event_mgr.send_event(event_type, payload=payload)
    
    # Process events (this will trigger logging)
    print("\nProcessing events (will be logged)...")
    event_mgr.process_events()
    
    print(f"\nTotal events logged: {logger.get_event_count()}")
    print("Event logging example completed.\n")

def custom_event_example():
    """Demonstrate custom event types and handlers."""
    print("=== Custom Event Example ===")
    
    # Define custom event types (using high numbers to avoid conflicts)
    CUSTOM_EVENT_ANALYSIS_START = 10001
    CUSTOM_EVENT_ANALYSIS_COMPLETE = 10002
    CUSTOM_EVENT_USER_INTERACTION = 10003
    
    event_mgr = EventManager()
    
    # Create a custom analysis component
    class AnalysisComponent:
        def __init__(self, event_manager):
            self.event_mgr = event_manager
            self.analysis_running = False
            
            # Register as event handler
            self.event_mgr.register_handler("analysis_component", self.handle_events)
            
            # Subscribe to custom events
            self.event_mgr.subscribe_to_events("analysis_component", [
                CUSTOM_EVENT_ANALYSIS_START,
                CUSTOM_EVENT_USER_INTERACTION
            ])
        
        def handle_events(self, events):
            for event in events:
                if event.type == CUSTOM_EVENT_ANALYSIS_START:
                    self.start_analysis(event)
                elif event.type == CUSTOM_EVENT_USER_INTERACTION:
                    self.handle_user_interaction(event)
        
        def start_analysis(self, event):
            print(f"Starting analysis: {event.payload}")
            self.analysis_running = True
            
            # Simulate analysis work
            time.sleep(0.1)
            
            # Send completion event
            self.event_mgr.send_event(
                CUSTOM_EVENT_ANALYSIS_COMPLETE,
                payload={"status": "completed", "results": "analysis_data.json"}
            )
            self.analysis_running = False
        
        def handle_user_interaction(self, event):
            interaction = event.payload
            print(f"User interaction: {interaction}")
    
    # Create analysis component
    analysis = AnalysisComponent(event_mgr)
    
    # Create a results handler
    def results_handler(events):
        for event in events:
            if event.type == CUSTOM_EVENT_ANALYSIS_COMPLETE:
                print(f"Analysis completed: {event.payload}")
    
    event_mgr.register_handler("results_handler", results_handler)
    event_mgr.subscribe_to_event("results_handler", CUSTOM_EVENT_ANALYSIS_COMPLETE)
    
    # Send custom events
    print("Triggering custom events...")
    
    # Start analysis
    event_mgr.send_event(
        CUSTOM_EVENT_ANALYSIS_START,
        payload={"type": "RDF", "cutoff": 5.0}
    )
    
    # User interactions
    event_mgr.send_event(
        CUSTOM_EVENT_USER_INTERACTION,
        payload={"action": "zoom", "target": "protein"}
    )
    
    event_mgr.send_event(
        CUSTOM_EVENT_USER_INTERACTION,
        payload={"action": "select", "atoms": [10, 20, 30]}
    )
    
    # Process events
    print("\nProcessing custom events...")
    event_mgr.process_events()
    
    print("Custom event example completed.\n")

def main():
    """Run all event system examples."""
    print("VIAMD Event System Examples")
    print("=" * 50)
    
    try:
        # Run examples
        basic_event_example()
        molecular_event_example()
        event_logging_example()
        custom_event_example()
        
        print("All examples completed successfully!")
        
    except Exception as e:
        print(f"Error running examples: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()