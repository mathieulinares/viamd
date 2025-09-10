#include <stdio.h>
#include <stdlib.h>
#include <core/md_allocator.h>
#include <core/md_tracking_allocator.h>
#include <core/md_array.h>

// Test for the tracking allocator fix
// This test verifies that the tracking allocator can handle reallocation of pointers
// that were not originally allocated through it (mixed allocator usage)

int main() {
    printf("Testing tracking allocator robustness...\n");
    
    // Create a heap allocator and tracking allocator
    md_allocator_i* heap_alloc = md_get_heap_allocator();
    md_allocator_i* tracking_alloc = md_tracking_allocator_create(heap_alloc);
    
    printf("✓ Created allocators\n");
    
    // Test 1: Allocate with heap allocator, then reallocate with tracking allocator
    void* ptr1 = heap_alloc->realloc(heap_alloc->inst, NULL, 0, 100, __FILE__, __LINE__);
    if (!ptr1) {
        printf("✗ Failed to allocate with heap allocator\n");
        return 1;
    }
    printf("✓ Allocated 100 bytes with heap allocator\n");
    
    // This should not crash with the fix - previously this would cause SIGILL
    void* ptr2 = tracking_alloc->realloc(tracking_alloc->inst, ptr1, 100, 200, __FILE__, __LINE__);
    if (!ptr2) {
        printf("✗ Failed to reallocate with tracking allocator\n");
        return 1;
    }
    printf("✓ Reallocated to 200 bytes with tracking allocator (this would have crashed before the fix)\n");
    
    // Test 2: Free the reallocated pointer with tracking allocator
    tracking_alloc->realloc(tracking_alloc->inst, ptr2, 200, 0, __FILE__, __LINE__);
    printf("✓ Freed pointer with tracking allocator\n");
    
    // Test 3: Test with arrays (the original use case that caused the issue)
    md_array(int) test_array = 0;
    
    // Add some items using heap allocator first
    md_array_push(test_array, 42, heap_alloc);
    md_array_push(test_array, 43, heap_alloc);
    printf("✓ Added items to array with heap allocator\n");
    
    // Now try to add more items with tracking allocator - this would trigger reallocation
    // and previously cause the crash
    md_array_push(test_array, 44, tracking_alloc);
    md_array_push(test_array, 45, tracking_alloc);
    printf("✓ Added more items to array with tracking allocator (mixed allocator usage)\n");
    
    // Verify the array contents
    if (md_array_size(test_array) == 4 && 
        test_array[0] == 42 && test_array[1] == 43 && 
        test_array[2] == 44 && test_array[3] == 45) {
        printf("✓ Array contents are correct\n");
    } else {
        printf("✗ Array contents are incorrect\n");
        return 1;
    }
    
    // Clean up
    if (test_array) {
        tracking_alloc->realloc(tracking_alloc->inst, md_array_header(test_array), 
                               sizeof(md_array_header_t) + md_array_capacity(test_array) * sizeof(int), 
                               0, __FILE__, __LINE__);
    }
    
    printf("All tests passed! The tracking allocator fix is working correctly.\n");
    return 0;
}