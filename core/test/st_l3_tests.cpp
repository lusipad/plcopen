// L3 process-image acceptance (approved st-l3-semantics sections 0-4).
//
// This file is intentionally the RED contract for the caller-owned process
// image, snapshot codec and force queue.  It must not emulate those services
// in the test: the VM/executor boundary is part of the public contract.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "st/st.h"

namespace
{

bool g_freeze_allocations = false;
unsigned long long g_frozen_allocations = 0;

} // namespace

void *operator new(std::size_t size)
{
    if(g_freeze_allocations) {
        ++g_frozen_allocations;
    }
    return std::malloc(size ? size : 1);
}

void *operator new[](std::size_t size)
{
    if(g_freeze_allocations) {
        ++g_frozen_allocations;
    }
    return std::malloc(size ? size : 1);
}

void operator delete(void *pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void *pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void *pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void *pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace
{

using namespace plcopen::core;

constexpr std::int64_t kPeriodNs = 1000000;
constexpr std::int64_t kBudget = 10000000;

int failures = 0;

void fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    ++failures;
}

void check(bool condition, const char *name)
{
    if(!condition) {
        fail(name);
    }
}

bool has_code(const st::CompileResult &result, st::DiagCode code)
{
    for(const st::Diagnostic &diagnostic : result.diagnostics) {
        if(diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

std::string program(const char *declarations, const char *body)
{
    std::string source = "PROGRAM Main\n";
    source += declarations;
    source += "\n";
    source += body;
    source += "\nEND_PROGRAM\n";
    return source;
}

std::uint64_t read_le(const unsigned char *bytes, std::size_t size)
{
    std::uint64_t value = 0;
    for(std::size_t index = 0; index < size; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

struct Rig
{
    st::CompileResult compiled;
    st::ProcessImage image;
    st::Instance instance;
    alignas(8) unsigned char image_storage[65536] = {};
    alignas(8) unsigned char instance_storage[65536] = {};

    bool build(const std::string &source,
               const st::CompileOptions &options = st::CompileOptions{})
    {
        compiled = st::compile(source, options);
        if(!compiled.ok) {
            for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
                std::printf("  diag %d:%d %s\n", diagnostic.line,
                            diagnostic.column, diagnostic.message.c_str());
            }
            return false;
        }
        if(image.load(compiled.program, image_storage,
                      sizeof(image_storage)) != rt::ErrorCode::ok) {
            return false;
        }
        return instance.load(compiled.program, instance_storage,
                             sizeof(instance_storage), kPeriodNs, &image) ==
               rt::ErrorCode::ok;
    }

    st::ScanError scan(std::int64_t budget = kBudget)
    {
        return instance.scan(budget);
    }

    std::int64_t i64(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0
                   ? -424242
                   : instance.value_i64(static_cast<std::size_t>(index));
    }

    bool submit_input(const unsigned char *bytes, std::size_t size,
                      std::uint64_t version)
    {
        return image.submit_input(bytes, size, version) == rt::ErrorCode::ok;
    }

    bool output(std::vector<unsigned char> &bytes, std::uint64_t &version)
    {
        bytes.resize(compiled.program.process_image.output_bytes);
        std::size_t written = 0;
        return image.output_snapshot(bytes.data(), bytes.size(), written,
                                     version) == rt::ErrorCode::ok &&
               written == bytes.size();
    }

    bool memory(std::vector<unsigned char> &bytes, std::uint64_t &version)
    {
        bytes.resize(compiled.program.process_image.memory_bytes);
        std::size_t written = 0;
        return image.memory_snapshot(bytes.data(), bytes.size(), written,
                                     version) == rt::ErrorCode::ok &&
               written == bytes.size();
    }
};

// L3-A01/D01/D02: X/B/W/D/L are canonical little-endian mappings.  Input
// bit/byte aliasing is deliberate; each writable location is disjoint.
void address_width_and_endian_oracle()
{
    Rig rig;
    check(rig.build(program(
              "VAR\n"
              "  IX AT %IX0.0 : BOOL; IB AT %IB0 : BYTE;\n"
              "  IW AT %IW2 : WORD; ID AT %ID4 : DWORD; IL AT %IL8 : LWORD;\n"
              "  QB AT %QB0 : BYTE; QX AT %QX1.0 : BOOL;\n"
              "  QW AT %QW2 : WORD; QD AT %QD4 : DWORD; QL AT %QL8 : LWORD;\n"
              "  MB AT %MB0 : BYTE; MX AT %MX1.0 : BOOL;\n"
              "  MW AT %MW2 : WORD; MD AT %MD4 : DWORD; ML AT %ML8 : LWORD;\n"
              "  SeenX : BOOL; SeenB : BYTE; SeenW : WORD;\n"
              "  SeenD : DWORD; SeenL : LWORD;\n"
              "END_VAR",
              "SeenX := IX; SeenB := IB; SeenW := IW; SeenD := ID; "
              "SeenL := IL; "
              "QB := BYTE#16#A5; QX := TRUE; QW := WORD#16#1234; "
              "QD := DWORD#16#89ABCDEF; QL := LWORD#16#0123456789ABCDEF; "
              "MB := BYTE#16#5A; MX := TRUE; MW := WORD#16#4321; "
              "MD := DWORD#16#76543210; ML := LWORD#16#FEDCBA9876543210;")),
          "L3-A01 width program builds");

    const unsigned char input[] = {
        0xA5, 0x00, 0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x89,
        0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
    };
    check(rig.submit_input(input, sizeof(input), 17),
          "L3-A01 input version submits");
    check(rig.scan() == st::ScanError::ok, "L3-A01 width scan succeeds");
    check(rig.i64("SeenX") == 1 && rig.i64("SeenB") == 0xA5 &&
              rig.i64("SeenW") == 0x1234 &&
              static_cast<std::uint64_t>(rig.i64("SeenD")) == 0x89ABCDEFULL &&
              static_cast<std::uint64_t>(rig.i64("SeenL")) ==
                  0x0123456789ABCDEFULL,
          "L3-A01 input mapping is little endian with low bit first");

    std::vector<unsigned char> output;
    std::uint64_t version = 0;
    check(rig.output(output, version) && version == 1 && output.size() == 16,
          "L3-A01 output publishes one complete version");
    const unsigned char expected[] = {
        0xA5, 0x01, 0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x89,
        0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
    };
    check(output.size() == sizeof(expected) &&
              std::memcmp(output.data(), expected, sizeof(expected)) == 0,
          "L3-A01 output mapping is host-independent little endian");

    std::vector<unsigned char> memory;
    check(rig.memory(memory, version) && version == 1 && memory.size() == 16,
          "L3-A01 memory publishes one complete version");
    const unsigned char expected_memory[] = {
        0x5A, 0x01, 0x21, 0x43, 0x10, 0x32, 0x54, 0x76,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    };
    check(memory.size() == sizeof(expected_memory) &&
              std::memcmp(memory.data(), expected_memory,
                          sizeof(expected_memory)) == 0,
          "L3-A01 M X/B/W/D/L mapping is canonical little endian");
}

// L3-A02/D03/D04: writable overlap is always illegal; exact and X/byte input
// aliases are legal, while inconsistent partial input aliases are rejected.
void overlap_alignment_and_type_rejection()
{
    const st::CompileResult legal_input_alias = st::compile(program(
        "VAR a AT %IW0 : WORD; b AT %IW0 : WORD; "
        "x AT %IX2.3 : BOOL; byte AT %IB2 : BYTE; out : WORD; END_VAR",
        "IF x THEN out := a + b + byte; END_IF;"));
    check(legal_input_alias.ok, "L3-D02/D03 compatible input aliases allowed");

    const char *writable_overlap[] = {
        "VAR a AT %QB0 : BYTE; b AT %QB0 : BYTE; END_VAR",
        "VAR a AT %QX0.0 : BOOL; b AT %QB0 : BYTE; END_VAR",
        "VAR a AT %MW0 : WORD; b AT %MB1 : BYTE; END_VAR",
    };
    for(const char *declarations : writable_overlap) {
        const st::CompileResult result =
            st::compile(program(declarations, ";"));
        check(!result.ok &&
                  has_code(result, st::DiagCode::sema_process_image_overlap),
              "L3-D03 writable overlap has stable diagnostic");
    }

    const st::CompileResult inconsistent_input = st::compile(program(
        "VAR byte AT %IB0 : BYTE; word AT %IW0 : WORD; END_VAR", ";"));
    check(!inconsistent_input.ok,
          "L3-D03 inconsistent partial input alias rejected");

    const char *misaligned[] = {
        "VAR x AT %QW1 : WORD; END_VAR",
        "VAR x AT %QD2 : DWORD; END_VAR",
        "VAR x AT %QL4 : LWORD; END_VAR",
    };
    for(const char *declarations : misaligned) {
        check(!st::compile(program(declarations, ";")).ok,
              "L3-D04 W/D/L alignment enforced");
    }

    const char *wrong_width[] = {
        "VAR x AT %IX0.0 : BYTE; END_VAR",
        "VAR x AT %IB0 : WORD; END_VAR",
        "VAR x AT %IW0 : DWORD; END_VAR",
        "VAR x AT %ID0 : LWORD; END_VAR",
        "VAR x AT %IL0 : DWORD; END_VAR",
    };
    for(const char *declarations : wrong_width) {
        const st::CompileResult result =
            st::compile(program(declarations, ";"));
        check(!result.ok && has_code(result, st::DiagCode::sema_type_mismatch),
              "L3-D01 located type width enforced");
    }

    check(!st::compile(program("VAR x AT %IX0.8 : BOOL; END_VAR", ";")).ok,
          "L3-D02 X bit number is bounded to zero through seven");
    check(!st::compile(program("VAR x AT %IB65536 : BYTE; END_VAR", ";")).ok,
          "L3-A02 default image bound rejects first byte past 64 KiB");
    check(!st::compile(
               program("VAR x AT %IB4294967295 : BYTE; END_VAR", ";"))
               .ok,
          "L3-A02 address plus width overflow is rejected at load time");
}

// L3-A02: I/Q/M limits are independent and checked before offset arithmetic
// can wrap.  The requested force/retain tables are independently bounded.
void capacity_and_range_rejection()
{
    st::CompileOptions options;
    options.max_input_image_bytes = 8;
    check(!st::compile(program("VAR x AT %IL8 : LWORD; END_VAR", ";"),
                       options)
               .ok,
          "L3 input image capacity enforced");

    options = st::CompileOptions{};
    options.max_output_image_bytes = 8;
    check(!st::compile(program("VAR x AT %QL8 : LWORD; END_VAR", ";"),
                       options)
               .ok,
          "L3 output image capacity enforced");

    options = st::CompileOptions{};
    options.max_memory_image_bytes = 8;
    check(!st::compile(program("VAR x AT %ML8 : LWORD; END_VAR", ";"),
                       options)
               .ok,
          "L3 memory image capacity enforced");

    options = st::CompileOptions{};
    options.max_retain_entries = 1;
    const st::CompileResult retain = st::compile(
        program("VAR RETAIN a AT %MB0 : BYTE; b AT %MB1 : BYTE; END_VAR",
                ";"),
        options);
    check(!retain.ok && has_code(retain, st::DiagCode::capacity_exceeded),
          "L3 retain entry capacity has stable bounded failure");

    Rig force;
    options = st::CompileOptions{};
    options.max_force_entries = 1;
    check(force.build(program(
              "VAR i AT %IB0 : BYTE; q AT %QB0 : BYTE; END_VAR", "q := i;"),
              options),
          "L3 bounded force table builds");
    const unsigned char one = 1;
    check(force.image.queue_force("i", st::builtin::byte_, &one, 1) ==
              rt::ErrorCode::ok,
          "L3 first force entry queues");
    check(force.image.queue_force("q", st::builtin::byte_, &one, 1) ==
              rt::ErrorCode::capacity_exceeded,
          "L3 force queue capacity is explicit");
}

// L3-A03/D05: executor input publication may race a scan, but both reads in
// one scan must observe exactly one frozen version.
void input_snapshot_is_frozen_per_scan()
{
    Rig rig;
    check(rig.build(program(
              "VAR input AT %ID0 : DWORD; first : DWORD; second : DWORD; "
              "i : DINT; spin : DINT; same : BOOL; END_VAR",
              "first := input; FOR i := 1 TO 2000 DO spin := spin + i; "
              "END_FOR; second := input; same := first = second;")),
          "L3-D05 snapshot program builds");
    unsigned char initial[4] = {1, 1, 1, 1};
    check(rig.submit_input(initial, sizeof(initial), 1),
          "L3-D05 initial input submits");

    std::atomic<bool> stop{false};
    std::atomic<bool> submit_failed{false};
    std::atomic<bool> ready{false};
    std::atomic<unsigned> submissions{0};
    std::thread executor([&]() {
        std::uint64_t version = 2;
        ready.store(true, std::memory_order_release);
        while(!stop.load(std::memory_order_relaxed)) {
            unsigned char next[4];
            std::memset(next, static_cast<int>(version & 0xFFU), sizeof(next));
            if(rig.image.submit_input(next, sizeof(next), version) !=
               rt::ErrorCode::ok) {
                submit_failed.store(true, std::memory_order_relaxed);
                return;
            }
            ++submissions;
            ++version;
        }
    });
    while(!ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for(int scan = 0; scan < 200; ++scan) {
        if(rig.scan() != st::ScanError::ok || rig.i64("same") != 1) {
            fail("L3-D05 one scan observes one input version");
            break;
        }
    }
    stop.store(true, std::memory_order_relaxed);
    executor.join();
    check(!submit_failed.load(std::memory_order_relaxed),
          "L3-D05 concurrent input submit succeeds");
    check(submissions.load(std::memory_order_relaxed) != 0,
          "L3-D05 executor published while scans were active");
}

// L3-A03/D06: a reader racing the scan sees either the old complete Q image
// or the new complete Q image, never a mix of the two DWORD writes.
void output_publication_is_atomic()
{
    Rig rig;
    check(rig.build(program(
              "VAR left AT %QD0 : DWORD; right AT %QD4 : DWORD; "
              "counter : DWORD; END_VAR",
              "counter := counter + 1; left := counter; right := NOT counter;")),
          "L3-D06 output publication program builds");

    std::atomic<bool> stop{false};
    std::atomic<bool> torn{false};
    std::atomic<bool> ready{false};
    std::atomic<unsigned> observed{0};
    std::thread reader([&]() {
        ready.store(true, std::memory_order_release);
        while(!stop.load(std::memory_order_relaxed)) {
            unsigned char bytes[8]{};
            std::size_t written = 0;
            std::uint64_t version = 0;
            if(rig.image.output_snapshot(bytes, sizeof(bytes), written,
                                         version) != rt::ErrorCode::ok ||
               written != sizeof(bytes)) {
                torn.store(true, std::memory_order_relaxed);
                return;
            }
            if(version == 0) {
                continue;
            }
            ++observed;
            const std::uint32_t left =
                static_cast<std::uint32_t>(read_le(bytes, 4));
            const std::uint32_t right =
                static_cast<std::uint32_t>(read_le(bytes + 4, 4));
            if(right != static_cast<std::uint32_t>(~left)) {
                torn.store(true, std::memory_order_relaxed);
                return;
            }
        }
    });
    while(!ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for(int scan = 0; scan < 2000; ++scan) {
        if(rig.scan() != st::ScanError::ok) {
            fail("L3-D06 writer scan succeeds");
            break;
        }
    }
    for(int attempt = 0;
        attempt < 10000 && observed.load(std::memory_order_relaxed) == 0 &&
        !torn.load(std::memory_order_relaxed);
        ++attempt) {
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_relaxed);
    reader.join();
    check(!torn.load(std::memory_order_relaxed) &&
              observed.load(std::memory_order_relaxed) != 0,
          "L3-D06 output version is never torn");
}

// L3-A04/D06/D07: Q and M are transactional; ordinary VM variables retain
// L0's prior-write-on-fault behavior.  A successful M commit persists scans.
void fault_rolls_back_images_not_ordinary_variables()
{
    Rig fault;
    check(fault.build(program(
              "VAR q AT %QB0 : BYTE; m AT %MB0 : BYTE; ordinary : DINT; "
              "zero : DINT; boom : DINT; END_VAR",
              "q := 9; m := 7; ordinary := 5; boom := 1 / zero;")),
          "L3-A04 fault program builds");
    check(fault.scan() == st::ScanError::division_by_zero,
          "L3-A04 scan faults after image writes");
    std::vector<unsigned char> q;
    std::vector<unsigned char> m;
    std::uint64_t q_version = 99;
    std::uint64_t m_version = 99;
    check(fault.output(q, q_version) && q_version == 0 && q[0] == 0,
          "L3-A04 fault discards Q shadow");
    check(fault.memory(m, m_version) && m_version == 0 && m[0] == 0,
          "L3-A04 fault discards M shadow");
    check(fault.i64("ordinary") == 5,
          "L3-A04 ordinary assignment before fault remains visible");

    Rig success;
    check(success.build(program("VAR m AT %MD0 : DWORD := 1; END_VAR",
                                "m := m + 1;")),
          "L3-D07 M persistence program builds");
    check(success.scan() == st::ScanError::ok &&
              success.scan() == st::ScanError::ok,
          "L3-D07 two successful scans complete");
    std::uint64_t version = 0;
    check(success.memory(m, version) && version == 2 &&
              read_le(m.data(), 4) == 3,
          "L3-D07 committed M state persists across scans");
}

std::string snapshot_source(const char *extra = "")
{
    std::string source =
        "PROGRAM Main\n"
        "VAR RETAIN retained AT %MW0 : WORD := WORD#16#1234; END_VAR\n"
        "VAR PERSISTENT durable AT %MD4 : DWORD := DWORD#16#55667788; "
        "END_VAR\n";
    source += extra;
    source += ";\nEND_PROGRAM\n";
    return source;
}

// L3-A05/D08-D10: snapshot bytes are deterministic and versioned.  RETAIN
// requires the same project fingerprint; malformed/version-mismatched input
// leaves declaration defaults intact rather than partially restoring.
void retain_round_trip_and_rejection()
{
    Rig producer;
    check(producer.build(snapshot_source()), "L3-D08 snapshot source builds");
    check(producer.scan() == st::ScanError::ok,
          "L3-D08 snapshot source reaches boundary");

    unsigned char first[4096]{};
    unsigned char second[4096]{};
    std::size_t first_size = 0;
    std::size_t second_size = 0;
    check(producer.image.encode_snapshot(st::SnapshotKind::retain, first,
                                         sizeof(first), first_size) ==
              st::SnapshotError::ok,
          "L3-D08 RETAIN encodes into caller buffer");
    check(producer.image.encode_snapshot(st::SnapshotKind::retain, second,
                                         sizeof(second), second_size) ==
                  st::SnapshotError::ok &&
              first_size == second_size && first_size != 0 &&
              std::memcmp(first, second, first_size) == 0,
          "L3-D08 RETAIN encoding is deterministic");

    Rig restored;
    check(restored.build(snapshot_source()), "L3-D08 restore target builds");
    st::SnapshotReport report{};
    check(restored.image.restore_snapshot(st::SnapshotKind::retain, first,
                                          first_size, report) ==
                  st::SnapshotError::ok &&
              report.restored == 1 && report.rejected == 0,
          "L3-D08 matching RETAIN restores exactly one item");
    check(restored.scan() == st::ScanError::ok,
          "L3-D08 queued RETAIN applies at scan boundary");
    std::vector<unsigned char> memory;
    std::uint64_t memory_version = 0;
    check(restored.memory(memory, memory_version) &&
              read_le(memory.data(), 2) == 0x1234,
          "L3-D08 RETAIN value round trips");

    Rig changed_project;
    check(changed_project.build(snapshot_source(
              "VAR unrelated : DINT := 1; END_VAR\n")),
          "L3-D08 changed-fingerprint target builds");
    report = st::SnapshotReport{};
    check(changed_project.image.restore_snapshot(st::SnapshotKind::retain,
                                                 first, first_size, report) ==
                  st::SnapshotError::fingerprint_mismatch &&
              report.restored == 0 && report.defaulted == 1,
          "L3-D08 RETAIN fingerprint mismatch rejects all and defaults");

    bool version_rejected = false;
    for(std::size_t index = 0; index < first_size && !version_rejected;
        ++index) {
        unsigned char mutated[4096];
        std::memcpy(mutated, first, first_size);
        mutated[index] ^= 0xFFU;
        Rig probe;
        if(!probe.build(snapshot_source())) {
            fail("L3-D08 version probe target builds");
            break;
        }
        report = st::SnapshotReport{};
        version_rejected =
            probe.image.restore_snapshot(st::SnapshotKind::retain, mutated,
                                         first_size, report) ==
            st::SnapshotError::version_mismatch;
    }
    check(version_rejected, "L3-D08 schema version is encoded and checked");

    Rig truncated;
    check(truncated.build(snapshot_source()),
          "L3-D08 malformed snapshot target builds");
    report = st::SnapshotReport{};
    check(truncated.image.restore_snapshot(st::SnapshotKind::retain, first,
                                           first_size - 1, report) ==
                  st::SnapshotError::malformed &&
              report.restored == 0 && report.defaulted == 1,
          "L3-D08 truncated snapshot rejected with declaration default");
}

// L3-A05/D09: PERSISTENT may cross a project-fingerprint change, but only a
// stable variable ID with the exact same type and length is restored.
void persistent_stable_id_and_type_drift()
{
    Rig producer;
    check(producer.build(snapshot_source()),
          "L3-D09 persistent producer builds");
    check(producer.scan() == st::ScanError::ok,
          "L3-D09 persistent producer scans");
    unsigned char snapshot[4096]{};
    std::size_t size = 0;
    check(producer.image.encode_snapshot(st::SnapshotKind::persistent,
                                         snapshot, sizeof(snapshot), size) ==
              st::SnapshotError::ok,
          "L3-D09 PERSISTENT encodes");

    Rig compatible;
    check(compatible.build(snapshot_source(
              "VAR new_variable : DINT := 7; END_VAR\n")),
          "L3-D09 changed project with stable variable builds");
    st::SnapshotReport report{};
    check(compatible.image.restore_snapshot(st::SnapshotKind::persistent,
                                            snapshot, size, report) ==
                  st::SnapshotError::ok &&
              !report.fingerprint_match && report.restored == 1 &&
              report.rejected == 0,
          "L3-D09 stable persistent ID survives fingerprint change");

    const std::string drifted =
        "PROGRAM Main\n"
        "VAR RETAIN retained AT %MW0 : WORD := WORD#16#1234; END_VAR\n"
        "VAR PERSISTENT durable AT %ML8 : LWORD := LWORD#16#99; END_VAR\n"
        ";\nEND_PROGRAM\n";
    Rig incompatible;
    check(incompatible.build(drifted),
          "L3-D09 type-drift persistent target builds");
    report = st::SnapshotReport{};
    check(incompatible.image.restore_snapshot(st::SnapshotKind::persistent,
                                              snapshot, size, report) ==
                  st::SnapshotError::ok &&
              report.restored == 0 && report.rejected == 1 &&
              report.defaulted == 1,
          "L3-D09 persistent type/length drift rejects only that item");
    check(incompatible.scan() == st::ScanError::ok,
          "L3-D09 partial persistent restore applies at boundary");
    std::vector<unsigned char> memory;
    std::uint64_t version = 0;
    check(incompatible.memory(memory, version) &&
              read_le(memory.data() + 8, 8) == 0x99,
          "L3-D09 rejected persistent item keeps declaration default");
}

// L3-A06/D11-D13: queued force changes apply at the next boundary, override
// reads and final publication, and release reveals current input/computation.
void force_priority_release_and_validation()
{
    Rig rig;
    check(rig.build(program(
              "VAR i AT %IB0 : BYTE; q AT %QB0 : BYTE; m AT %MB0 : BYTE; "
              "seen_i : BYTE; seen_q : BYTE; seen_m : BYTE; END_VAR",
              "seen_i := i; q := i; m := m + 1; seen_q := q; seen_m := m;")),
          "L3-D11 force program builds");
    const unsigned char input_one = 1;
    check(rig.submit_input(&input_one, 1, 1),
          "L3-D11 ordinary input submits");

    const unsigned char forced_i = 5;
    const unsigned char forced_q = 9;
    const unsigned char forced_m = 7;
    check(rig.image.queue_force("i", st::builtin::byte_, &forced_i, 1) ==
                  rt::ErrorCode::ok &&
              rig.image.queue_force("q", st::builtin::byte_, &forced_q, 1) ==
                  rt::ErrorCode::ok &&
              rig.image.queue_force("m", st::builtin::byte_, &forced_m, 1) ==
                  rt::ErrorCode::ok,
          "L3-D11 I/Q/M forces queue");
    check(rig.scan() == st::ScanError::ok,
          "L3-D11 forced scan succeeds at next boundary");
    check(rig.i64("seen_i") == 5 && rig.i64("seen_q") == 9 &&
              rig.i64("seen_m") == 7,
          "L3-D11 force overrides located reads");
    std::vector<unsigned char> output;
    std::vector<unsigned char> memory;
    std::uint64_t version = 0;
    check(rig.output(output, version) && output[0] == 9,
          "L3-D11 Q force overrides final publication");
    check(rig.memory(memory, version) && memory[0] == 7,
          "L3-D11 M force overrides final publication");

    check(rig.image.queue_release("i") == rt::ErrorCode::ok &&
              rig.image.queue_release("q") == rt::ErrorCode::ok &&
              rig.image.queue_release("m") == rt::ErrorCode::ok,
          "L3-D12 force releases queue");
    const unsigned char input_two = 2;
    check(rig.submit_input(&input_two, 1, 2),
          "L3-D12 replacement input submits");
    check(rig.scan() == st::ScanError::ok && rig.i64("seen_i") == 2 &&
              rig.i64("seen_q") == 2 && rig.i64("seen_m") == 8,
          "L3-D12 release uses current input and current computation");
    check(rig.output(output, version) && output[0] == 2,
          "L3-D12 release does not replay old Q force");
    check(rig.memory(memory, version) && memory[0] == 8,
          "L3-D12 release does not replay old M force");

    const unsigned char partial = 1;
    check(rig.image.queue_force("missing", st::builtin::byte_, &partial, 1) ==
              rt::ErrorCode::invalid_argument,
          "L3-D13 undeclared force target rejected");
    check(rig.image.queue_force("q", st::builtin::word, &partial, 1) ==
              rt::ErrorCode::invalid_argument,
          "L3-D13 wrong force type rejected");

    Rig wide;
    check(wide.build(program("VAR q AT %QW0 : WORD; END_VAR", ";")),
          "L3-D13 wide force target builds");
    check(wide.image.queue_force("q", st::builtin::word, &partial, 1) ==
              rt::ErrorCode::invalid_argument,
          "L3-D13 partial-width force rejected");
}

// L3-A07: scan, image swap, force-mask application and version publication
// are all allocation-free after load.  Compilation is deterministic together
// with the process-image fingerprint and layout sizes.
void rt_and_determinism()
{
    const std::string source = program(
        "VAR i AT %ID0 : DWORD; q AT %QD0 : DWORD; m AT %MD0 : DWORD; "
        "n : DWORD; END_VAR",
        "n := n + i + m; q := n; m := m + 1;");
    Rig rig;
    check(rig.build(source), "L3-A07 RT program builds");
    const unsigned char input[4] = {1, 0, 0, 0};
    check(rig.submit_input(input, sizeof(input), 1),
          "L3-A07 RT input submits");
    check(rig.scan() == st::ScanError::ok, "L3-A07 RT warmup succeeds");
    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    for(int scan = 0; scan < 1000; ++scan) {
        if(rig.scan() != st::ScanError::ok) {
            g_freeze_allocations = false;
            fail("L3-A07 frozen scan succeeds");
            return;
        }
    }
    g_freeze_allocations = false;
    check(g_frozen_allocations == 0,
          "L3-A07 scan and process-image transaction allocate zero bytes");

    const st::CompileResult first = st::compile(source);
    check(first.ok, "L3-A07 deterministic source compiles");
    for(int repeat = 0; repeat < 100; ++repeat) {
        const st::CompileResult again = st::compile(source);
        if(!again.ok || again.program.code != first.program.code ||
           again.program.constants != first.program.constants ||
           again.program.process_image.fingerprint !=
               first.program.process_image.fingerprint ||
           again.program.process_image.input_bytes !=
               first.program.process_image.input_bytes ||
           again.program.process_image.output_bytes !=
               first.program.process_image.output_bytes ||
           again.program.process_image.memory_bytes !=
               first.program.process_image.memory_bytes) {
            fail("L3-A07 compile and image layout are deterministic");
            break;
        }
    }
    const st::CompileResult changed = st::compile(program(
        "VAR i AT %ID0 : DWORD; q AT %QD0 : DWORD; "
        "m AT %MD0 : DWORD; renamed : DWORD; END_VAR",
        "renamed := i + m; q := renamed;"));
    check(changed.ok && changed.program.process_image.fingerprint !=
                            first.program.process_image.fingerprint,
          "L3-D08 project fingerprint changes with the declared schema");
}

// L3 introduction may change bytecode and storage ABI, but current-revision
// L0-L2 source behavior must remain available after recompilation.
void lower_layer_source_regression()
{
    struct SourceCase
    {
        const char *name;
        const char *source;
        const char *result;
        long long expected;
    };
    const SourceCase cases[] = {
        {"L0 control",
         "PROGRAM Main VAR x : DINT; y : DINT; END_VAR "
         "x := 2 + 3 * 4; IF x = 14 THEN y := 1; END_IF; END_PROGRAM",
         "y", 1},
        {"L1a scalar",
         "PROGRAM Main VAR x : SINT; y : DINT; END_VAR "
         "x := 127; x := x + 1; y := SINT_TO_DINT(x); END_PROGRAM",
         "y", -128},
        {"L1b aggregate",
         "TYPE Pair : STRUCT a : DINT; b : DINT; END_STRUCT; END_TYPE "
         "PROGRAM Main VAR p : Pair := (a := 3, b := 4); out : DINT; "
         "END_VAR out := p.a + p.b; END_PROGRAM",
         "out", 7},
        {"L2a standard FB",
         "PROGRAM Main VAR timer : TON; out : BOOL; END_VAR "
         "timer(IN := TRUE, PT := T#0s); out := timer.Q; END_PROGRAM",
         "out", 1},
        {"L2b user function",
         "FUNCTION UserAdd : DINT VAR_INPUT a : DINT; b : DINT; END_VAR "
         "UserAdd := a + b; END_FUNCTION "
         "PROGRAM Main VAR out : DINT; END_VAR out := UserAdd(5, 6); "
         "END_PROGRAM",
         "out", 11},
    };
    for(const SourceCase &test : cases) {
        Rig rig;
        if(!rig.build(test.source)) {
            fail(test.name);
            continue;
        }
        check(rig.scan() == st::ScanError::ok &&
                  rig.i64(test.result) == test.expected,
              test.name);
    }
}

} // namespace

int main()
{
    address_width_and_endian_oracle();
    overlap_alignment_and_type_rejection();
    capacity_and_range_rejection();
    input_snapshot_is_frozen_per_scan();
    output_publication_is_atomic();
    fault_rolls_back_images_not_ordinary_variables();
    retain_round_trip_and_rejection();
    persistent_stable_id_and_type_drift();
    force_priority_release_and_validation();
    rt_and_determinism();
    lower_layer_source_regression();
    if(failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l3 tests passed\n");
    return 0;
}
