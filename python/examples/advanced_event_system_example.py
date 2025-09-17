#!/usr/bin/env python3
"""
Advanced VIAMD Event System Example

This example demonstrates advanced event system patterns for creating
sophisticated Python components that integrate with VIAMD's event system.

Features demonstrated:
- Event filtering and priority handling
- Multi-component event orchestration
- Real-time analysis pipelines using events
- Event-driven state management
- Custom event middleware
- Performance monitoring through events
"""

import sys
import time
import threading
from typing import Dict, List, Any, Optional
from dataclasses import dataclass
from collections import defaultdict
import pyviamd
from pyviamd.event import EventManager, EventType, EventPayloadType

# Custom event types for advanced scenarios
ANALYSIS_PIPELINE_START = 20001
ANALYSIS_PIPELINE_STEP = 20002
ANALYSIS_PIPELINE_COMPLETE = 20003
PERFORMANCE_METRIC = 20004
STATE_CHANGE = 20005
USER_COMMAND = 20006

@dataclass
class PerformanceMetric:
    """Performance metric data structure."""
    component: str
    operation: str
    duration_ms: float
    memory_mb: float
    timestamp: float

class EventMiddleware:
    """Middleware for event processing pipeline."""
    
    def __init__(self):
        self.filters = []
        self.transforms = []
        self.validators = []
    
    def add_filter(self, filter_func):
        """Add event filter function."""
        self.filters.append(filter_func)
    
    def add_transform(self, transform_func):
        """Add event transform function."""
        self.transforms.append(transform_func)
    
    def add_validator(self, validator_func):
        """Add event validator function."""
        self.validators.append(validator_func)
    
    def process_events(self, events):
        """Process events through middleware pipeline."""
        processed_events = events[:]
        
        # Apply filters
        for filter_func in self.filters:
            processed_events = [e for e in processed_events if filter_func(e)]
        
        # Apply transforms
        for transform_func in self.transforms:
            processed_events = [transform_func(e) for e in processed_events]
        
        # Apply validators
        for validator_func in self.validators:
            processed_events = [e for e in processed_events if validator_func(e)]
        
        return processed_events

class AnalysisPipelineComponent:
    """Advanced analysis component using event-driven architecture."""
    
    def __init__(self, name: str, event_manager: EventManager):
        self.name = name
        self.event_mgr = event_manager
        self.state = "idle"
        self.current_pipeline = None
        self.results = {}
        self.performance_metrics = []
        
        # Register event handler
        self.event_mgr.register_handler(f"pipeline_{name}", self.handle_events)
        
        # Subscribe to pipeline events
        self.event_mgr.subscribe_to_events(f"pipeline_{name}", [
            ANALYSIS_PIPELINE_START,
            ANALYSIS_PIPELINE_STEP,
            USER_COMMAND,
        ])
    
    def handle_events(self, events):
        """Handle pipeline events."""
        for event in events:
            if event.type == ANALYSIS_PIPELINE_START:
                self.start_pipeline(event.payload)
            elif event.type == ANALYSIS_PIPELINE_STEP:
                self.process_pipeline_step(event.payload)
            elif event.type == USER_COMMAND:
                self.handle_user_command(event.payload)
    
    def start_pipeline(self, config):
        """Start analysis pipeline."""
        print(f"{self.name}: Starting pipeline with config: {config}")
        self.state = "running"
        self.current_pipeline = config
        
        start_time = time.time()
        
        # Simulate pipeline steps
        steps = config.get("steps", ["preprocessing", "analysis", "postprocessing"])
        for i, step in enumerate(steps):
            step_start = time.time()
            
            # Send step event
            self.event_mgr.send_event(
                ANALYSIS_PIPELINE_STEP,
                payload={
                    "component": self.name,
                    "step": step,
                    "step_index": i,
                    "total_steps": len(steps)
                }
            )
            
            # Simulate work
            time.sleep(0.05)
            
            # Record performance metric
            step_duration = (time.time() - step_start) * 1000
            metric = PerformanceMetric(
                component=self.name,
                operation=step,
                duration_ms=step_duration,
                memory_mb=10.0 + i * 2.0,  # Simulated memory usage
                timestamp=time.time()
            )
            
            self.event_mgr.send_event(
                PERFORMANCE_METRIC,
                payload=metric.__dict__
            )
        
        # Complete pipeline
        total_duration = (time.time() - start_time) * 1000
        self.state = "complete"
        
        self.event_mgr.send_event(
            ANALYSIS_PIPELINE_COMPLETE,
            payload={
                "component": self.name,
                "duration_ms": total_duration,
                "results": f"results_{self.name}.json"
            }
        )
    
    def process_pipeline_step(self, step_data):
        """Process individual pipeline step."""
        if step_data.get("component") == self.name:
            step = step_data["step"]
            print(f"{self.name}: Processing step '{step}' ({step_data['step_index']+1}/{step_data['total_steps']})")
    
    def handle_user_command(self, command):
        """Handle user commands."""
        action = command.get("action")
        if action == "pause" and self.state == "running":
            print(f"{self.name}: Pausing pipeline")
            self.state = "paused"
        elif action == "resume" and self.state == "paused":
            print(f"{self.name}: Resuming pipeline")
            self.state = "running"
        elif action == "stop":
            print(f"{self.name}: Stopping pipeline")
            self.state = "stopped"

class PerformanceMonitor:
    """Component for monitoring system performance through events."""
    
    def __init__(self, event_manager: EventManager):
        self.event_mgr = event_manager
        self.metrics: List[PerformanceMetric] = []
        self.component_stats: Dict[str, Dict] = defaultdict(dict)
        
        # Register event handler
        self.event_mgr.register_handler("performance_monitor", self.handle_events)
        self.event_mgr.subscribe_to_event("performance_monitor", PERFORMANCE_METRIC)
    
    def handle_events(self, events):
        """Handle performance metric events."""
        for event in events:
            if event.type == PERFORMANCE_METRIC:
                self.record_metric(event.payload)
    
    def record_metric(self, metric_data):
        """Record performance metric."""
        component = metric_data["component"]
        operation = metric_data["operation"]
        duration = metric_data["duration_ms"]
        
        # Update component statistics
        if component not in self.component_stats:
            self.component_stats[component] = {
                "total_operations": 0,
                "total_duration": 0,
                "operations": defaultdict(list)
            }
        
        stats = self.component_stats[component]
        stats["total_operations"] += 1
        stats["total_duration"] += duration
        stats["operations"][operation].append(duration)
        
        # Store metric
        metric = PerformanceMetric(**metric_data)
        self.metrics.append(metric)
        
        print(f"Performance: {component}.{operation} took {duration:.2f}ms")
    
    def get_summary(self):
        """Get performance summary."""
        summary = {}
        for component, stats in self.component_stats.items():
            avg_duration = stats["total_duration"] / stats["total_operations"]
            summary[component] = {
                "total_operations": stats["total_operations"],
                "average_duration_ms": avg_duration,
                "operations": {
                    op: {
                        "count": len(durations),
                        "avg_duration_ms": sum(durations) / len(durations),
                        "min_duration_ms": min(durations),
                        "max_duration_ms": max(durations)
                    }
                    for op, durations in stats["operations"].items()
                }
            }
        return summary

class StateManager:
    """Component for managing application state through events."""
    
    def __init__(self, event_manager: EventManager):
        self.event_mgr = event_manager
        self.state = {
            "active_components": [],
            "analysis_status": "idle",
            "user_selections": [],
            "system_mode": "interactive"
        }
        self.state_history = []
        
        # Register event handler
        self.event_mgr.register_handler("state_manager", self.handle_events)
        
        # Subscribe to state-changing events
        self.event_mgr.subscribe_to_events("state_manager", [
            ANALYSIS_PIPELINE_START,
            ANALYSIS_PIPELINE_COMPLETE,
            STATE_CHANGE,
            EventType.ViamdSelectionMaskChanged,
        ])
    
    def handle_events(self, events):
        """Handle state-changing events."""
        for event in events:
            old_state = self.state.copy()
            
            if event.type == ANALYSIS_PIPELINE_START:
                self.state["analysis_status"] = "running"
                component = event.payload.get("component", "unknown")
                if component not in self.state["active_components"]:
                    self.state["active_components"].append(component)
            
            elif event.type == ANALYSIS_PIPELINE_COMPLETE:
                self.state["analysis_status"] = "complete"
                component = event.payload.get("component", "unknown")
                if component in self.state["active_components"]:
                    self.state["active_components"].remove(component)
            
            elif event.type == STATE_CHANGE:
                # Direct state changes
                changes = event.payload
                for key, value in changes.items():
                    if key in self.state:
                        self.state[key] = value
            
            elif event.type == EventType.ViamdSelectionMaskChanged:
                selection = event.payload.get("atoms", [])
                self.state["user_selections"] = selection
            
            # Record state change
            if self.state != old_state:
                self.state_history.append({
                    "timestamp": time.time(),
                    "old_state": old_state,
                    "new_state": self.state.copy(),
                    "trigger_event": event.type
                })
                
                print(f"State change: {old_state} -> {self.state}")
    
    def get_current_state(self):
        """Get current application state."""
        return self.state.copy()
    
    def get_state_history(self):
        """Get state change history."""
        return self.state_history.copy()

def advanced_event_orchestration_example():
    """Demonstrate advanced event orchestration."""
    print("=== Advanced Event Orchestration Example ===")
    
    # Create event manager
    event_mgr = EventManager()
    
    # Create middleware
    middleware = EventMiddleware()
    
    # Add filters for high-priority events only
    middleware.add_filter(lambda event: event.type in [
        ANALYSIS_PIPELINE_START, ANALYSIS_PIPELINE_COMPLETE, PERFORMANCE_METRIC
    ])
    
    # Create components
    analysis1 = AnalysisPipelineComponent("RDF_Analysis", event_mgr)
    analysis2 = AnalysisPipelineComponent("RMSD_Analysis", event_mgr)
    perf_monitor = PerformanceMonitor(event_mgr)
    state_mgr = StateManager(event_mgr)
    
    # Create orchestrator
    def orchestration_handler(events):
        filtered_events = middleware.process_events(events)
        if filtered_events:
            print(f"Orchestrator: Processing {len(filtered_events)} filtered events")
    
    event_mgr.register_handler("orchestrator", orchestration_handler)
    event_mgr.subscribe_to_events("orchestrator", [
        ANALYSIS_PIPELINE_START,
        ANALYSIS_PIPELINE_COMPLETE,
        PERFORMANCE_METRIC
    ])
    
    # Start analysis pipelines
    print("\nStarting analysis pipelines...")
    
    event_mgr.send_event(
        ANALYSIS_PIPELINE_START,
        payload={
            "component": "RDF_Analysis",
            "steps": ["load_data", "compute_rdf", "save_results"]
        }
    )
    
    event_mgr.send_event(
        ANALYSIS_PIPELINE_START,
        payload={
            "component": "RMSD_Analysis", 
            "steps": ["align_structures", "compute_rmsd", "plot_results"]
        }
    )
    
    # Send user commands
    event_mgr.send_event(
        USER_COMMAND,
        payload={"action": "pause", "target": "RDF_Analysis"}
    )
    
    # Process events
    print("\nProcessing orchestrated events...")
    event_mgr.process_events()
    
    # Get performance summary
    print("\nPerformance Summary:")
    summary = perf_monitor.get_summary()
    for component, stats in summary.items():
        print(f"  {component}: {stats['total_operations']} ops, "
              f"avg {stats['average_duration_ms']:.2f}ms")
    
    # Get state information
    print(f"\nCurrent State: {state_mgr.get_current_state()}")
    print(f"State Changes: {len(state_mgr.get_state_history())}")
    
    print("Advanced orchestration example completed.\n")

def real_time_analysis_example():
    """Demonstrate real-time analysis using events."""
    print("=== Real-time Analysis Example ===")
    
    event_mgr = EventManager()
    
    # Create real-time analyzer
    class RealTimeAnalyzer:
        def __init__(self):
            self.frame_count = 0
            self.analysis_results = []
            
            event_mgr.register_handler("realtime_analyzer", self.handle_events)
            event_mgr.subscribe_to_event("realtime_analyzer", EventType.ViamdFrameTick)
        
        def handle_events(self, events):
            for event in events:
                if event.type == EventType.ViamdFrameTick:
                    self.analyze_frame(event.payload)
        
        def analyze_frame(self, frame_data):
            self.frame_count += 1
            
            # Simulate real-time analysis
            result = {
                "frame": frame_data.get("frame", self.frame_count),
                "energy": 100 + self.frame_count * 0.5,  # Simulated energy
                "rmsd": 2.0 + (self.frame_count % 10) * 0.1,  # Simulated RMSD
                "timestamp": time.time()
            }
            
            self.analysis_results.append(result)
            
            print(f"Frame {result['frame']}: Energy={result['energy']:.1f}, RMSD={result['rmsd']:.2f}")
            
            # Trigger alerts for anomalies
            if result["energy"] > 150:
                event_mgr.send_event(
                    STATE_CHANGE,
                    payload={"alert": "high_energy", "frame": result["frame"]}
                )
    
    # Create analyzer
    analyzer = RealTimeAnalyzer()
    
    # Simulate trajectory frames
    print("Simulating real-time trajectory analysis...")
    for frame in range(10):
        event_mgr.send_event(
            EventType.ViamdFrameTick,
            payload={"frame": frame, "time": frame * 0.1}
        )
    
    # Process events
    event_mgr.process_events()
    
    print(f"Analyzed {analyzer.frame_count} frames")
    print("Real-time analysis example completed.\n")

def main():
    """Run advanced event system examples."""
    print("Advanced VIAMD Event System Examples")
    print("=" * 50)
    
    try:
        advanced_event_orchestration_example()
        real_time_analysis_example()
        
        print("All advanced examples completed successfully!")
        
    except Exception as e:
        print(f"Error running examples: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()