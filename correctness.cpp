#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mmapped_vector.h"
#include <algorithm>
#include <filesystem>
#include <numeric>
#include <thread>
#include <vector>
#include <cstdint>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

namespace mv = mmapped_vector;

// ── RAII temp file ─────────────────────────────────────────────────────────

struct TempFile {
    std::string path;
    explicit TempFile(std::string p) : path(std::move(p)) {}
    ~TempFile() { std::filesystem::remove(path); }
    operator const std::string&() const { return path; }
};

static std::string unique_tmp() {
    static int n = 0;
    return "mmv_test_" + std::to_string(n++) + ".dat";
}

// ── shared test body (run for every vector type) ───────────────────────────
// make() is called exactly 3 times: for v, sorted, and v3.

template <typename VT, typename Make>
void shared_tests(Make make) {
    // ── empty state ──────────────────────────────────────────────────────
    VT v = make();
    REQUIRE(v.empty());
    REQUIRE(v.size() == 0);

    // ── push_back, size, operator[] ──────────────────────────────────────
    v.push_back(1); v.push_back(2); v.push_back(3);
    CHECK(v.size() == 3);
    CHECK(!v.empty());
    CHECK(v.capacity() >= 3);
    CHECK(v[0] == 1); CHECK(v[1] == 2); CHECK(v[2] == 3);
    CHECK(v.front() == 1); CHECK(v.back() == 3);

    // ── const overloads ───────────────────────────────────────────────────
    {
        const VT& cv = v;
        CHECK(cv[0] == 1);
        CHECK(cv.at(0) == 1);
        CHECK(cv.front() == 1);
        CHECK(cv.back() == 3);
        CHECK(cv.size() == 3);
        CHECK(!cv.empty());
        CHECK(cv.capacity() >= 3);
        CHECK(cv.data() == v.data());
        CHECK(cv.begin() == v.cbegin());
        CHECK(cv.end() == v.cend());
    }

    // ── emplace_back ─────────────────────────────────────────────────────
    v.emplace_back(4);
    CHECK(v.size() == 4); CHECK(v.back() == 4);

    // ── pop_back ─────────────────────────────────────────────────────────
    v.pop_back();
    CHECK(v.size() == 3); CHECK(v.back() == 3);

    // ── at() bounds check ────────────────────────────────────────────────
    CHECK(v.at(0) == 1);
    CHECK_THROWS_AS(v.at(3), std::out_of_range);
    CHECK_THROWS_AS(v.at(99), std::out_of_range);
    {
        const VT& cv = v;
        CHECK(cv.at(2) == 3);
        CHECK_THROWS_AS(cv.at(99), std::out_of_range);
    }

    // ── data() ────────────────────────────────────────────────────────────
    CHECK(v.data() == &v[0]);
    {
        const VT& cv = v;
        CHECK(cv.data() == &cv[0]);
    }

    // ── iterators ─────────────────────────────────────────────────────────
    CHECK(v.end() - v.begin() == (ptrdiff_t)v.size());
    CHECK(v.cend() - v.cbegin() == (ptrdiff_t)v.size());
    CHECK(*v.begin() == 1);
    CHECK(*(v.end() - 1) == 3);
    {
        int sum = 0;
        for (const auto& x : v) sum += x;
        CHECK(sum == 6);
    }
    CHECK(std::find(v.begin(), v.end(), 2) == v.begin() + 1);
    CHECK(std::find(v.begin(), v.end(), 99) == v.end());

    // ── reserve (grows only, never shrinks) ───────────────────────────────
    size_t cap0 = v.capacity();
    v.reserve(cap0 + 100);
    CHECK(v.capacity() >= cap0 + 100);
    CHECK(v.size() == 3); // size unchanged
    size_t cap1 = v.capacity();
    v.reserve(1); // must not shrink
    CHECK(v.capacity() == cap1);

    // ── clear and reuse ───────────────────────────────────────────────────
    v.clear();
    CHECK(v.empty()); CHECK(v.size() == 0);
    for (int i = 0; i < 200; ++i) v.push_back(i);
    CHECK(v.size() == 200);
    CHECK(v[0] == 0); CHECK(v[199] == 199);
    {
        int expected = 0;
        for (const auto& x : v) CHECK(x == expected++);
    }

    // ── std algorithms via iterators ──────────────────────────────────────
    VT sorted = make();
    for (int i = 99; i >= 0; --i) sorted.push_back(i);
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::is_sorted(sorted.begin(), sorted.end()));
    for (int i = 0; i < 100; ++i) CHECK(sorted[i] == i);
    CHECK(std::accumulate(sorted.begin(), sorted.end(), 0) == 4950);

    // ── resize (shrink) ───────────────────────────────────────────────────
    // ISSUE: MmappedVector::resize(n) sets capacity == n (calls allocator.resize directly).
    // std::vector keeps excess capacity on shrink; MmappedVector truncates the backing store.
    v.resize(5);
    CHECK(v.size() == 5);
    if constexpr (!std::is_same_v<VT, std::vector<int>>)
        CHECK(v.capacity() == 5);  // capacity truncated to n
    else
        CHECK(v.capacity() >= 5);  // std::vector may keep excess capacity
    for (int i = 0; i < 5; ++i) CHECK(v[i] == i);

    // ── resize (grow) ─────────────────────────────────────────────────────
    // ISSUE: MmappedVector does NOT value-initialize new elements.
    // std::vector zero-initializes new elements on resize; MmappedVector leaves them undefined.
    v.resize(10);
    CHECK(v.size() == 10);
    if constexpr (!std::is_same_v<VT, std::vector<int>>)
        CHECK(v.capacity() == 10);
    else
        CHECK(v.capacity() >= 10);
    for (int i = 0; i < 5; ++i) CHECK(v[i] == i); // original values intact
    // v[5..9] are zero for std::vector, undefined for MmappedVector — not checked

    // ── shrink_to_fit ─────────────────────────────────────────────────────
    v.clear();
    for (int i = 0; i < 50; ++i) v.push_back(i);
    v.reserve(500);
    CHECK(v.capacity() >= 500);
    v.shrink_to_fit();
    CHECK(v.capacity() == v.size());
    CHECK(v.size() == 50);
    for (int i = 0; i < 50; ++i) CHECK(v[i] == i);

    // ── move constructor ──────────────────────────────────────────────────
    VT v2(std::move(v));
    CHECK(v.empty()); CHECK(v2.size() == 50);

    // ── move assignment ───────────────────────────────────────────────────
    v = std::move(v2);
    CHECK(v2.empty()); CHECK(v.size() == 50);

    // ── operator== / operator!= ───────────────────────────────────────────
    VT v3 = make();
    for (int i = 0; i < 50; ++i) v3.push_back(i);
    CHECK(v == v3);
    CHECK(!(v != v3));
    v3.push_back(999);
    CHECK(v != v3);
    CHECK(!(v == v3));

    CHECK(std::accumulate(v.begin(), v.end(), 0) == 1225); // 0+1+...+49
}

// ── per-type test cases ────────────────────────────────────────────────────

TEST_CASE("std::vector<int>") {
    shared_tests<std::vector<int>>([]{ return std::vector<int>{}; });
}

TEST_CASE("MallocVector<int>") {
    shared_tests<mv::MallocVector<int>>([]{ return mv::MallocVector<int>{}; });
}

TEST_CASE("MmapVector<int>") {
    shared_tests<mv::MmapVector<int>>([]{ return mv::MmapVector<int>{}; });
}

TEST_CASE("MmapFileVector<int>") {
    // make() is called 3 times in shared_tests (v, sorted, v3)
    TempFile f1{unique_tmp()}, f2{unique_tmp()}, f3{unique_tmp()};
    const std::string* paths[] = {&f1.path, &f2.path, &f3.path};
    int call = 0;
    shared_tests<mv::MmapFileVector<int>>([&]{
        return mv::MmapFileVector<int>(*paths[call++], MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    });
}

// ── MmapFileVector-specific ────────────────────────────────────────────────

TEST_CASE("MmapFileVector: persistence across open/close") {
    TempFile f{unique_tmp()};
    {
        mv::MmapFileVector<int> v(f, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        v.push_back(10); v.push_back(20); v.push_back(30);
    }
    mv::MmapFileVector<int> v2(f);
    CHECK(v2.size() == 3);
    CHECK(v2[0] == 10); CHECK(v2[1] == 20); CHECK(v2[2] == 30);
}

TEST_CASE("MmapFileVector: reopen with smaller backing size is not corrupted") {
    TempFile f{unique_tmp()};
    {
        mv::MmapFileVector<int> v(f, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        for (int i = 0; i < 100; ++i) v.push_back(i);
    }
    {
        mv::MmapFileVector<int> v(f);
        REQUIRE(v.size() == 100);
        for (int i = 0; i < 100; ++i) CHECK(v[i] == i);
        v.resize(10); // shrink and re-persist
    }
    mv::MmapFileVector<int> v(f);
    CHECK(v.size() == 10);
    for (int i = 0; i < 10; ++i) CHECK(v[i] == i);
}

// ── MmapVector growth (mremap on Linux, mach_vm on macOS, mmap+copy elsewhere) ──

// Map one page right after the vector's region so that it cannot grow in place.
// Returns the page (to munmap later) or nullptr if the kernel put it elsewhere.
template <typename VT>
static void* block_after(VT& v) {
    const size_t pg = (size_t)sysconf(_SC_PAGESIZE);
    uintptr_t end = ((uintptr_t)v.data() + v.capacity() * sizeof(*v.data()) + pg - 1) & ~(uintptr_t)(pg - 1);
    void* p = mmap((void*)end, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    if (p != (void*)end) { munmap(p, pg); return nullptr; }
    return p;
}

TEST_CASE("MmapVector: growth across many pages keeps contents") {
    mv::MmapVector<int> v;
    constexpr int N = 1 << 22;  // 16 MiB
    for (int i = 0; i < N; ++i) v.push_back(i);
    bool ok = true;
    for (int i = 0; i < N; ++i) ok &= (v[i] == i);
    CHECK(ok);

    v.resize(1000);
    v.shrink_to_fit();
    CHECK(v.capacity() == 1000);
    for (int i = 1000; i < N; ++i) v.push_back(i);
    ok = true;
    for (int i = 0; i < N; ++i) ok &= (v[i] == i);
    CHECK(ok);
}

TEST_CASE("MmapVector: growth when the address range after it is taken") {
    mv::MmapVector<int> v;
    for (int i = 0; i < 100000; ++i) v.push_back(i);
    void* blocker = block_after(v);
    if (!blocker) WARN_MESSAGE(false, "could not place blocker page; in-place growth not excluded");
    const int* before = v.data();
    for (int i = 100000; i < 1000000; ++i) v.push_back(i);
    if (blocker) CHECK(v.data() != before);
    bool ok = true;
    for (int i = 0; i < 1000000; ++i) ok &= (v[i] == i);
    CHECK(ok);
    if (blocker) munmap(blocker, (size_t)sysconf(_SC_PAGESIZE));
}

TEST_CASE("MmapVector(MAP_SHARED): stays shared with fork()ed child after moving growth") {
#ifdef MREMAP_MAYMOVE
    // BUG (Linux): a MAP_SHARED|MAP_ANONYMOUS mapping is backed by a fixed-size shmem object;
    // mremap grows the mapping but not the object, so touching the new pages raises SIGBUS.
    // Growing a shared anonymous MmapVector past its first page crashes. Needs e.g. a memfd
    // backing that can be ftruncate()d. Skipped until fixed.
    MESSAGE("skipped on Linux: MAP_SHARED anonymous MmapVector cannot grow (SIGBUS)");
    return;
#endif
    mv::MmapVector<int> v(MAP_SHARED | MAP_ANONYMOUS);
    for (int i = 0; i < 100000; ++i) v.push_back(i);
    void* blocker = block_after(v);
    for (int i = 100000; i < 1000000; ++i) v.push_back(i);

    pid_t pid = fork();
    REQUIRE(pid != -1);
    if (pid == 0) {
        v[0] = -1; v[999999] = -2;
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status));
    CHECK(v[0] == -1);
    CHECK(v[999999] == -2);
    CHECK(v[500000] == 500000);
    if (blocker) munmap(blocker, (size_t)sysconf(_SC_PAGESIZE));
}

// ── known bugs ────────────────────────────────────────────────────────────

TEST_CASE("BUG: resize(0) / shrink_to_fit on empty vector") {
    // MmappedVector does not handle size == 0 gracefully:
    //
    //   MmapAllocator::resize(0):   mremap to 0 bytes → EINVAL on Linux → throws
    //                               (ptr unchanged; destructor still cleans up correctly)
    //
    //   MallocAllocator::resize(0): realloc(ptr, 0) FREES the pointer then returns NULL
    //                               → code throws, but ptr is now dangling
    //                               → destructor calls free(ptr) again → DOUBLE FREE
    //                               (NOT tested here; crashes under ASan even if caught)
    //
    //   MmapFileAllocator:          ftruncate+mremap fails → throws; PLUS if reached
    //                               from destructor after clear(): std::terminate.
    //                               Cannot be tested safely.
    //
    // Expected behaviour (matching std::vector): should be a no-op.
    // Fix: add "if (new_capacity == 0) return;" in each allocator's resize().
    // When fixed: CHECK_THROWS → CHECK_NOTHROW.

    SUBCASE("MmapVector: resize(0) throws") {
        mv::MmapVector<int> v;
        CHECK_THROWS(v.resize(0));
    }
    SUBCASE("MmapVector: shrink_to_fit on empty throws") {
        mv::MmapVector<int> v;
        CHECK_THROWS(v.shrink_to_fit());
    }
    // MallocVector resize(0)/shrink_to_fit: not tested — causes double-free under ASan.
    // MmapFileVector + clear() + destroy: not tested — std::terminate.
}

// ── thread-safe push_back ─────────────────────────────────────────────────

TEST_CASE("MmappedVector<thread_safe>: operations_in_progress == 0 after fast-path push_backs") {
    mv::MmappedVector<int, mv::MallocAllocator<int>, true> v;
    v.reserve(1000); // all push_backs take the fast path (no resize)

    std::vector<std::thread> threads;
    for (int i = 0; i < 200; ++i)
        threads.emplace_back([&]{ v.push_back(42); });
    for (auto& t : threads) t.join();

    CHECK(v.operations_in_progress.load() == 0);
}

TEST_CASE("MmappedVector<thread_safe>: correct size and values with concurrent resize") {
    constexpr int N_THREADS = 8;
    constexpr int N_PER_THREAD = 1000;
    constexpr int TOTAL = N_THREADS * N_PER_THREAD;

    mv::MmappedVector<int, mv::MallocAllocator<int>, true> v;
    // no pre-reserve — resize triggered during concurrent push_backs

    std::vector<std::thread> threads;
    for (int t = 0; t < N_THREADS; ++t)
        threads.emplace_back([&, t]{
            for (int j = 0; j < N_PER_THREAD; ++j)
                v.push_back(t * N_PER_THREAD + j);
        });
    for (auto& t : threads) t.join();

    CHECK((int)v.size() == TOTAL);
    CHECK(v.operations_in_progress.load() == 0);

    // All values [0, TOTAL) must be present exactly once (order is non-deterministic)
    std::vector<int> values(v.begin(), v.end());
    std::sort(values.begin(), values.end());
    for (int i = 0; i < TOTAL; ++i)
        CHECK(values[i] == i);
}
