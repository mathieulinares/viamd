#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// Test for the tracking allocator fix
// This test verifies that the tracking allocator can handle mixed allocator usage
// without crashing with SIGILL

#include "ext/mdlib/src/core/md_tracking_allocator.h"
#include "ext/mdlib/src/core/md_allocator.h"

int main() {
    printf("=== Testing Tracking Allocator Fix ===\n\n");
    
    // Create a tracking allocator
    md_allocator_i* heap_alloc = md_get_heap_allocator();
    md_allocator_i* tracking_alloc = md_tracking_allocator_create(heap_alloc);
    
    printf("✓ Created tracking allocator\n");
    
    // Test 1: Allocate with regular heap allocator (simulating mixed usage)
    void* ptr1 = heap_alloc->realloc(heap_alloc->inst, NULL, 0, 100, __FILE__, __LINE__);
    assert(ptr1 != NULL);
    printf("✓ Allocated 100 bytes with heap allocator\n");
    
    // Test 2: Try to reallocate the heap-allocated pointer with tracking allocator
    // This would have caused SIGILL before the fix
    printf("✓ Attempting reallocation with tracking allocator (mixed usage)...\n");
    void* ptr2 = tracking_alloc->realloc(tracking_alloc->inst, ptr1, 100, 200, __FILE__, __LINE__);
    assert(ptr2 != NULL);
    printf("✓ Successfully reallocated to 200 bytes (SIGILL crash avoided!)\n");
    
    // Test 3: Normal reallocation with tracking allocator
    void* ptr3 = tracking_alloc->realloc(tracking_alloc->inst, ptr2, 200, 300, __FILE__, __LINE__);
    assert(ptr3 != NULL);
    printf("✓ Successfully reallocated tracked pointer to 300 bytes\n");
    
    // Test 4: Free with tracking allocator
    tracking_alloc->realloc(tracking_alloc->inst, ptr3, 300, 0, __FILE__, __LINE__);
    printf("✓ Successfully freed tracked allocation\n");
    
    // Test 5: Normal allocation workflow
    void* ptr4 = tracking_alloc->realloc(tracking_alloc->inst, NULL, 0, 50, __FILE__, __LINE__);
    assert(ptr4 != NULL);
    void* ptr5 = tracking_alloc->realloc(tracking_alloc->inst, ptr4, 50, 100, __FILE__, __LINE__);
    assert(ptr5 != NULL);
    tracking_alloc->realloc(tracking_alloc->inst, ptr5, 100, 0, __FILE__, __LINE__);
    printf("✓ Normal allocation workflow successful\n");
    
    // Test 6: Free untracked allocation with tracking allocator
    void* ptr6 = heap_alloc->realloc(heap_alloc->inst, NULL, 0, 80, __FILE__, __LINE__);
    assert(ptr6 != NULL);
    printf("✓ Allocated 80 bytes with heap allocator\n");
    
    // This should also work with the fix (graceful handling)
    tracking_alloc->realloc(tracking_alloc->inst, ptr6, 80, 0, __FILE__, __LINE__);
    printf("✓ Successfully freed untracked allocation (mixed usage)\n");
    
    // Clean up
    md_tracking_allocator_destroy(tracking_alloc);
    printf("✓ Destroyed tracking allocator\n\n");
    
    printf("=== ALL TESTS PASSED! ===\n");
    printf("✅ SIGILL crash in mixed allocator usage is fixed!\n");
    printf("✅ Tracking allocator now handles untracked pointers gracefully\n");
    printf("✅ VIAMD should no longer crash during UFF force field setup\n");
    
    return 0;
}