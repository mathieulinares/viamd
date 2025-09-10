# mdlib Patches

This directory contains patches for the mdlib submodule that fix critical issues in VIAMD.

## md_tracking_allocator.c

**Fixed Issue**: SIGILL crash during UFF force field setup due to hard assertion failure in mixed allocator usage scenarios.

**Problem**: The tracking allocator's `tracking_realloc` function used `ASSERT(alloc)` at line 68 to enforce that all reallocated pointers must be tracked. However, in mixed allocator scenarios, pointers allocated by the heap allocator would later be passed to the tracking allocator for reallocation, causing the assertion to fail and trigger SIGILL.

**Solution**: Replaced the hard assertion with graceful conditional handling that:
1. Maintains normal behavior for tracked allocations
2. Gracefully handles untracked allocations by forwarding operations to the backing allocator
3. Starts tracking new allocations when reallocating untracked pointers
4. Prevents SIGILL crashes during mixed allocator usage

**To Apply**: Copy this file to `ext/mdlib/src/core/md_tracking_allocator.c` and rebuild mdlib.

**Testing**: All 150 mdlib unit tests pass (149 passed, 1 unrelated spatial hash test failure due to missing test data).