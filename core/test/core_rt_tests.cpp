#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "rt/cycle.h"
#include "rt/error.h"
#include "rt/error_text.h"
#include "rt/spsc_queue.h"
#include "rt/static_vector.h"

namespace
{

bool g_alloc_frozen = false;
int g_alloc_violations = 0;

void *allocate(std::size_t size)
{
    if(g_alloc_frozen) {
        ++g_alloc_violations;
    }
    void *ptr = std::malloc(size);
    if(!ptr) {
        std::abort();
    }
    return ptr;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_error_text()
{
    using plcopen::core::rt::ErrorCode;
    using plcopen::core::rt::to_string;

    struct ErrorTextCase
    {
        ErrorCode code;
        const char *text;
    };

    const ErrorTextCase cases[] = {
        {ErrorCode::ok, "ok"},
        {ErrorCode::invalid_argument,
         "invalid_argument: a parameter value is out of its valid domain"},
        {ErrorCode::out_of_range,
         "out_of_range: a value exceeds an array or container bound"},
        {ErrorCode::capacity_exceeded,
         "capacity_exceeded: a fixed-size container is full"},
        {ErrorCode::infeasible,
         "infeasible: no solution exists for the given constraints"},
        {ErrorCode::precondition_failed,
         "precondition_failed: the object is not in the required state"},
        {ErrorCode::unsupported, "unsupported: this operation is not implemented"},
    };

    for(const ErrorTextCase &test : cases) {
        if(std::strcmp(to_string(test.code), test.text) != 0) {
            return fail("error text mapping");
        }
    }
    if(std::strcmp(to_string(static_cast<ErrorCode>(999)), "unknown error code") != 0) {
        return fail("unknown error text fallback");
    }
    return 0;
}

} // namespace

void *operator new(std::size_t size)
{
    return allocate(size);
}

void *operator new[](std::size_t size)
{
    return allocate(size);
}

void operator delete(void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void *ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void operator delete[](void *ptr, std::size_t) noexcept
{
    std::free(ptr);
}

int main()
{
    using namespace plcopen::core;

    const rt::CycleTick tick = rt::CycleTick::from_cycles(1'000'000'000);
    const rt::CycleDuration delta = rt::CycleDuration::from_cycles(250'000'000);
    if((tick + delta).cycles() != 1'250'000'000) {
        return fail("cycle tick long accumulation");
    }
    if(delta.to_nanoseconds(1000) != 250'000'000'000LL) {
        return fail("cycle duration boundary conversion");
    }

    rt::StaticVector<int, 3> values;
    if(values.capacity() != 3 || !values.empty()) {
        return fail("static vector initial state");
    }
    if(values.push_back(1) != rt::ErrorCode::ok || values.push_back(2) != rt::ErrorCode::ok ||
       values.push_back(3) != rt::ErrorCode::ok) {
        return fail("static vector push");
    }
    if(!values.full() || values.push_back(4) != rt::ErrorCode::capacity_exceeded) {
        return fail("static vector capacity");
    }
    if(values[0] != 1 || values[2] != 3) {
        return fail("static vector indexing");
    }
    const rt::StaticVector<int, 3> &const_values = values;
    if(const_values.data()[1] != 2 || values.data()[1] != 2 ||
       values.pop_back() != rt::ErrorCode::ok || values.size() != 2 || values.full()) {
        return fail("static vector pop and data access");
    }
    values.clear();
    if(!values.empty() || values.pop_back() != rt::ErrorCode::out_of_range) {
        return fail("static vector clear and underflow");
    }

    rt::SpscQueue<int, 3> queue;
    int out = 0;
    if(queue.pop(out)) {
        return fail("spsc empty pop");
    }
    if(!queue.push(10) || !queue.push(20) || !queue.push(30) || queue.push(40)) {
        return fail("spsc full semantics");
    }
    if(!queue.pop(out) || out != 10 || !queue.push(40)) {
        return fail("spsc wrap-around");
    }
    if(!queue.pop(out) || out != 20 || !queue.pop(out) || out != 30 || !queue.pop(out) ||
       out != 40 || queue.pop(out)) {
        return fail("spsc fifo order");
    }

    const rt::Result<int> ok = rt::Result<int>::success(42);
    const rt::Result<int> bad = rt::Result<int>::failure(rt::ErrorCode::infeasible);
    if(!ok || ok.value() != 42 || bad || bad.error() != rt::ErrorCode::infeasible) {
        return fail("result semantics");
    }
    if(check_error_text() != 0) {
        return 1;
    }

    g_alloc_frozen = true;
    for(int i = 0; i < 1000; ++i) {
        queue.push(i);
        queue.pop(out);
    }
    g_alloc_frozen = false;
    if(g_alloc_violations != 0) {
        return fail("allocation guard");
    }

    std::printf("PASS core rt tests\n");
    return 0;
}
