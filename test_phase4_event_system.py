#!/usr/bin/env python3
"""
Test Phase 4: Event System Integration

This test validates the Python event system bindings for VIAMD,
ensuring that custom Python components can properly integrate
with VIAMD's event-driven architecture.
"""

import sys
import os
import time

# Add parent directory to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

def test_basic_event_system():
    """Test basic event system functionality."""
    print("Testing basic event system...")
    
    try:
        import pyviamd
        from pyviamd.event import EventManager, EventType, EventPayloadType
        
        # Test event manager creation
        event_mgr = EventManager()
        print("✓ EventManager created successfully")
        
        # Test event handler registration
        events_received = []
        
        def test_handler(events):
            events_received.extend(events)
        
        event_mgr.register_handler("test_handler", test_handler)
        print("✓ Event handler registered successfully")
        
        # Test event subscription
        event_mgr.subscribe_to_event("test_handler", EventType.ViamdFrameTick)
        print("✓ Event subscription successful")
        
        # Test event sending
        event_mgr.send_event(EventType.ViamdFrameTick, payload={"test": "data"})
        print("✓ Event sent successfully")
        
        # Test event processing
        event_mgr.process_events()
        print("✓ Event processing successful")
        
        # Verify event was received
        if len(events_received) == 1:
            event = events_received[0]
            if event.type == EventType.ViamdFrameTick:
                print("✓ Event received correctly")
            else:
                print("✗ Wrong event type received")
                return False
        else:
            print(f"✗ Expected 1 event, got {len(events_received)}")
            return False
        
        return True
        
    except Exception as e:
        print(f"✗ Basic event system test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_event_types_and_constants():
    """Test event type constants and enums."""
    print("Testing event types and constants...")
    
    try:
        import pyviamd
        
        # Test EventType enum
        assert hasattr(pyviamd, 'EventType')
        assert hasattr(pyviamd.EventType, 'ViamdInitialize')
        assert hasattr(pyviamd.EventType, 'ViamdFrameTick')
        assert hasattr(pyviamd.EventType, 'ViamdTopologyInit')
        print("✓ EventType enum available with correct values")
        
        # Test EventPayloadType enum
        assert hasattr(pyviamd, 'EventPayloadType')
        assert hasattr(pyviamd.EventPayloadType, 'Undefined')
        assert hasattr(pyviamd.EventPayloadType, 'RepresentationInfo')
        print("✓ EventPayloadType enum available with correct values")
        
        # Test Event class
        assert hasattr(pyviamd, 'Event')
        print("✓ Event class available")
        
        return True
        
    except Exception as e:
        print(f"✗ Event types test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_high_level_event_api():
    """Test high-level Python event API."""
    print("Testing high-level event API...")
    
    try:
        from pyviamd.event import EventManager, EventLogger, MolecularEventHandler
        
        # Test EventManager
        event_mgr = EventManager()
        handlers = event_mgr.get_registered_handlers()
        print(f"✓ EventManager initialized, handlers: {handlers}")
        
        # Test EventLogger
        logger = EventLogger()
        initial_count = logger.get_event_count()
        print(f"✓ EventLogger initialized, initial count: {initial_count}")
        
        # Test MolecularEventHandler
        mol_handler = MolecularEventHandler()
        print("✓ MolecularEventHandler initialized")
        
        # Test callback assignment
        callback_called = []
        
        def test_callback(event):
            callback_called.append(event)
        
        mol_handler.on_topology_init = test_callback
        print("✓ Callback assignment successful")
        
        return True
        
    except Exception as e:
        print(f"✗ High-level API test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_event_filtering_and_subscriptions():
    """Test event filtering and subscription mechanisms."""
    print("Testing event filtering and subscriptions...")
    
    try:
        from pyviamd.event import EventManager, EventType
        
        event_mgr = EventManager()
        
        # Test multiple subscriptions
        topology_events = []
        frame_events = []
        
        def topology_handler(events):
            topology_events.extend([e for e in events if e.type == EventType.ViamdTopologyInit])
        
        def frame_handler(events):
            frame_events.extend([e for e in events if e.type == EventType.ViamdFrameTick])
        
        # Register handlers
        event_mgr.register_handler("topology_handler", topology_handler)
        event_mgr.register_handler("frame_handler", frame_handler)
        
        # Subscribe to different events
        event_mgr.subscribe_to_event("topology_handler", EventType.ViamdTopologyInit)
        event_mgr.subscribe_to_event("frame_handler", EventType.ViamdFrameTick)
        
        # Send mixed events
        event_mgr.send_event(EventType.ViamdTopologyInit, payload={"test": "topology"})
        event_mgr.send_event(EventType.ViamdFrameTick, payload={"test": "frame"})
        event_mgr.send_event(EventType.ViamdTopologyInit, payload={"test": "topology2"})
        
        # Process events
        event_mgr.process_events()
        
        # Verify filtering worked
        if len(topology_events) == 2 and len(frame_events) == 1:
            print("✓ Event filtering and subscriptions working correctly")
            return True
        else:
            print(f"✗ Filtering failed: got {len(topology_events)} topology, {len(frame_events)} frame events")
            return False
        
    except Exception as e:
        print(f"✗ Event filtering test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_event_payloads():
    """Test event payload handling."""
    print("Testing event payload handling...")
    
    try:
        from pyviamd.event import EventManager, EventType, EventPayloadType
        
        event_mgr = EventManager()
        
        # Test various payload types
        received_payloads = []
        
        def payload_handler(events):
            for event in events:
                received_payloads.append(event.payload)
        
        event_mgr.register_handler("payload_handler", payload_handler)
        event_mgr.subscribe_to_events("payload_handler", [
            EventType.ViamdFrameTick,
            EventType.ViamdTopologyInit
        ])
        
        # Send events with different payload types
        payloads_to_test = [
            {"string": "test", "number": 42},
            [1, 2, 3, 4, 5],
            "simple string",
            42,
            {"complex": {"nested": {"data": True}}}
        ]
        
        for i, payload in enumerate(payloads_to_test):
            event_mgr.send_event(
                EventType.ViamdFrameTick,
                EventPayloadType.RepresentationInfo,
                payload
            )
        
        # Process events
        event_mgr.process_events()
        
        # Verify payloads
        if len(received_payloads) == len(payloads_to_test):
            print("✓ All payloads received correctly")
            return True
        else:
            print(f"✗ Payload test failed: expected {len(payloads_to_test)}, got {len(received_payloads)}")
            return False
        
    except Exception as e:
        print(f"✗ Event payload test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_performance_and_stress():
    """Test event system performance under load."""
    print("Testing event system performance...")
    
    try:
        from pyviamd.event import EventManager, EventType
        
        event_mgr = EventManager()
        
        # Test high-volume event processing
        events_processed = 0
        
        def high_volume_handler(events):
            nonlocal events_processed
            events_processed += len(events)
        
        event_mgr.register_handler("high_volume_handler", high_volume_handler)
        event_mgr.subscribe_to_event("high_volume_handler", EventType.ViamdFrameTick)
        
        # Send many events
        num_events = 1000
        start_time = time.time()
        
        for i in range(num_events):
            event_mgr.send_event(EventType.ViamdFrameTick, payload={"frame": i})
        
        # Process events
        event_mgr.process_events()
        
        end_time = time.time()
        duration = end_time - start_time
        
        if events_processed == num_events:
            print(f"✓ Performance test passed: {num_events} events in {duration:.3f}s ({num_events/duration:.0f} events/sec)")
            return True
        else:
            print(f"✗ Performance test failed: expected {num_events}, processed {events_processed}")
            return False
        
    except Exception as e:
        print(f"✗ Performance test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """Run all Phase 4 tests."""
    print("VIAMD Phase 4: Event System Integration Tests")
    print("=" * 60)
    
    tests = [
        ("Basic Event System", test_basic_event_system),
        ("Event Types and Constants", test_event_types_and_constants),
        ("High-Level Event API", test_high_level_event_api),
        ("Event Filtering and Subscriptions", test_event_filtering_and_subscriptions),
        ("Event Payload Handling", test_event_payloads),
        ("Performance and Stress", test_performance_and_stress),
    ]
    
    passed = 0
    total = len(tests)
    
    for test_name, test_func in tests:
        print(f"\n--- {test_name} ---")
        try:
            if test_func():
                passed += 1
                print(f"✓ {test_name} PASSED")
            else:
                print(f"✗ {test_name} FAILED")
        except Exception as e:
            print(f"✗ {test_name} FAILED with exception: {e}")
    
    print(f"\n" + "=" * 60)
    print(f"Test Results: {passed}/{total} tests passed")
    
    if passed == total:
        print("🎉 ALL TESTS PASSED! Phase 4 implementation is working correctly.")
        return True
    else:
        print("❌ Some tests failed. Please check the implementation.")
        return False

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)