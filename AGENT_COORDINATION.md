# Agent Coordination Guidelines

## Quick Start for Agents

### Agent A: Infrastructure & Foundation
**Your Mission**: Build the Vulkan foundation that everyone else depends on
- **Start with**: Task 1.1 (CMakeLists.txt Vulkan integration)
- **Key deliverables**: VulkanContext, Buffer/Texture management, Memory allocation
- **Critical for**: All other agents - you're on the critical path!
- **Files to create**: `src/gfx/vk_context.*`, `src/gfx/vk_buffer.*`, `src/gfx/vk_texture.*`

### Agent B: Rendering Pipeline & Shaders
**Your Mission**: Convert the core rendering systems and shaders
- **Start with**: Task 3.1 (after Agent A completes Phase 2)
- **Key deliverables**: Immediate drawing, volume rendering, shader conversion
- **Dependencies**: Wait for Agent A's resource management (Week 6)
- **Files to modify**: `src/gfx/immediate_draw_utils.*`, `src/gfx/volumerender_utils.*`, `src/shaders/*`

### Agent C: Post-Processing & UI
**Your Mission**: Handle effects pipeline and ImGui integration
- **Start with**: Task 3.5 (can start in parallel with Agent B)
- **Key deliverables**: Post-processing chain, ImGui Vulkan backend
- **Dependencies**: Partial dependency on Agent A (Week 6), can work with Agent B
- **Files to modify**: `src/gfx/postprocessing_utils.*`, ImGui integration code

### Agent D: Testing & Validation
**Your Mission**: Ensure quality and performance throughout migration
- **Start with**: Setting up validation framework (Week 1)
- **Key deliverables**: Visual regression tests, performance benchmarks
- **Dependencies**: Can start immediately with framework, validation starts Week 13
- **Files to create**: Test harnesses, benchmark utilities

## Communication Protocols

### Daily Check-ins
Post updates in this format:
```
Agent X Update - Day Y:
✅ Completed: [specific tasks]
🔄 In Progress: [current work]
⏸️ Blocked: [dependencies/issues]
📋 Next: [tomorrow's plan]
🔗 Interface Changes: [any API/interface changes that affect others]
```

### Interface Change Notifications
When you modify any interface that other agents depend on:
1. Update the interface documentation
2. Notify affected agents immediately
3. Provide migration path if breaking changes

### File Ownership During Migration
To minimize merge conflicts:

**Agent A Files** (exclusive until Phase 2 complete):
- `CMakeLists.txt` (Vulkan sections)
- `src/gfx/vk_*` files
- `ext/` (Vulkan-related dependencies)

**Agent B Files**:
- `src/gfx/immediate_draw_utils.*`
- `src/gfx/volumerender_utils.*`
- `src/gfx/vk_pipeline.*`
- `src/shaders/volume/`
- `src/shaders/sdf/`

**Agent C Files**:
- `src/gfx/postprocessing_utils.*`
- `src/shaders/ssao/`, `src/shaders/dof/`, `src/shaders/temporal.*`, etc.
- ImGui integration files
- `src/gfx/vk_descriptor.*` (coordinate with Agent A)

**Agent D Files**:
- Test files and validation utilities
- Benchmark code
- Documentation

## Integration Schedule

### Week 3 Checkpoint: Foundation Ready
- **Agent A**: Core Vulkan context and utilities complete
- **Others**: Review interfaces, prepare for resource management phase

### Week 6 Checkpoint: Resources Ready  
- **Agent A**: Buffer/texture management complete
- **Agent B & C**: Begin rendering pipeline work
- **Agent D**: Validation framework ready

### Week 9 Checkpoint: First Rendering
- **Agent B**: Immediate drawing working in Vulkan
- **Agent C**: Post-processing framework established
- **Agent D**: Initial validation tests running

### Week 12 Checkpoint: Core Migration Complete
- **All**: Basic Vulkan rendering pipeline functional
- **Agent D**: Begin comprehensive testing phase

### Week 15 Checkpoint: UI Complete
- **Agent C**: ImGui fully migrated
- **Agent B**: Shader conversion underway
- **Agent D**: Performance benchmarking active

### Week 18 Checkpoint: Optimization Complete
- **Agent B**: All optimizations implemented
- **Agent D**: Full validation suite running
- **All**: Final integration testing

## Common Pitfalls to Avoid

### For Agent A (Infrastructure)
- Don't rush the foundation - other agents depend on solid interfaces
- Document memory management patterns clearly
- Test resource cleanup thoroughly to avoid leaks

### For Agent B (Rendering/Shaders)  
- Batch shader conversions - don't convert one-by-one
- Profile early and often - Vulkan performance characteristics differ from OpenGL
- Coordinate with Agent C on shared pipeline state

### For Agent C (Post-Processing/UI)
- ImGui Vulkan backend has different initialization requirements
- Post-processing effects may need render pass restructuring
- Coordinate descriptor set usage with other agents

### For Agent D (Testing)
- Start validation framework early, don't wait for complete features
- Automate as much as possible - manual testing won't scale
- Focus on visual fidelity first, performance second

## Emergency Procedures

### If Agent A Falls Behind
- **Impact**: Critical path blocker for everyone
- **Mitigation**: Agents B/C help with Agent A tasks, reassign if needed
- **Communication**: Daily updates required, escalate immediately

### If Integration Fails
- **Cause**: Usually interface mismatches between agents
- **Solution**: Emergency integration session, all agents present
- **Prevention**: Frequent interface validation, shared header files

### If Performance Regresses Significantly
- **Responsible**: Agent D to identify, Agent B to resolve
- **Escalation**: If > 20% regression, halt other work to fix
- **Process**: Profile → identify bottleneck → focused optimization

## Success Signals

### You're On Track If:
- ✅ Daily commits from each agent
- ✅ Integration checkpoints met on schedule  
- ✅ No blocking issues for more than 1 day
- ✅ Visual output matches OpenGL baseline
- ✅ Performance trends positive or neutral

### Warning Signs:
- ⚠️ Any agent silent for > 1 day
- ⚠️ Integration checkpoint missed
- ⚠️ Interface changes breaking other agent's work
- ⚠️ Memory leaks or crashes in validation
- ⚠️ Performance regression > 10%

Remember: This is a complex migration. Communication and coordination are as important as the technical implementation. When in doubt, ask questions and share updates frequently!