// Memory Manager stub for testing
// This is a placeholder implementation for CI testing

#include <cstdlib>
#include <cstddef>

namespace plc_runtime {
namespace memory {

class MemoryManager {
public:
    static MemoryManager& getInstance() {
        static MemoryManager instance;
        return instance;
    }
    
    void initialize() {
        // Stub implementation
    }
    
    void* allocate(size_t size) {
        return malloc(size);
    }
    
    void deallocate(void* ptr) {
        free(ptr);
    }
};

} // namespace memory
} // namespace plc_runtime