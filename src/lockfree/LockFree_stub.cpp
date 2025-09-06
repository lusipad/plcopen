// LockFree structures stub for testing
// This is a placeholder implementation for CI testing

#include <atomic>
#include <cstddef>

namespace plc_runtime {
namespace lockfree {

template<typename T>
class SPSCQueue {
public:
    SPSCQueue(size_t capacity) : capacity_(capacity) {
        // Stub implementation
    }
    
    bool push(const T& item) {
        // Stub implementation
        return true;
    }
    
    bool pop(T& item) {
        // Stub implementation
        return false;
    }
    
private:
    size_t capacity_;
};

} // namespace lockfree
} // namespace plc_runtime