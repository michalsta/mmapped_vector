#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mmapped_vector.h"
#include <filesystem>
#include <thread>
#include <vector>

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

// ── shared test body (runs for every vector type) ──────────────────────────

template <typename VT, typename Make>
void shared_tests(Make make) {
    VT v = make();
    CHECK(v.empty());
    CHECK(v.size() == 0);

    v.push_back(1); v.push_back(2); v.push_back(3);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1); CHECK(v[1] == 2); CHECK(v[2] == 3);
    CHECK(v.front() == 1); CHECK(v.back() == 3);

    v.emplace_back(4);
    CHECK(v.size() == 4); CHECK(v.back() == 4);

    v.pop_back();
    CHECK(v.size() == 3); CHECK(v.back() == 3);

    CHECK(v.at(0) == 1);
    CHECK_THROWS_AS(v.at(99), std::out_of_range);

    v.clear();
    CHECK(v.empty());

    for (int i = 0; i < 200; ++i) v.push_back(i);
    CHECK(v.size() == 200);
    CHECK(v[0] == 0); CHECK(v[199] == 199);
    CHECK(v.cend() == v.cbegin() + 200);

    VT v2(std::move(v));
    CHECK(v.empty()); CHECK(v2.size() == 200);

    v = std::move(v2);
    CHECK(v2.empty()); CHECK(v.size() == 200);

    VT v3 = make();
    for (int i = 0; i < 200; ++i) v3.push_back(i);
    CHECK(v == v3);
    v3.push_back(999);
    CHECK(v != v3);
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
    TempFile f1{unique_tmp()}, f2{unique_tmp()};
    // make() is called twice inside shared_tests (once for v, once for v3)
    int call = 0;
    shared_tests<mv::MmapFileVector<int>>([&]{
        const std::string& path = (call++ == 0) ? f1.path : f2.path;
        return mv::MmapFileVector<int>(path, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    });
}

TEST_CASE("MmapFileVector<int>: persistence across open/close") {
    TempFile f{unique_tmp()};
    {
        mv::MmapFileVector<int> v(f, MAP_SHARED, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        v.push_back(10); v.push_back(20); v.push_back(30);
    }
    mv::MmapFileVector<int> v2(f);
    CHECK(v2.size() == 3);
    CHECK(v2[0] == 10); CHECK(v2[1] == 20); CHECK(v2[2] == 30);
}

TEST_CASE("MmappedVector<thread_safe>: operations_in_progress == 0 after concurrent push_backs") {
    mv::MmappedVector<int, mv::MallocAllocator<int>, true> v;
    v.reserve(1000);  // pre-reserve so all push_backs take the fast path

    std::vector<std::thread> threads;
    for (int i = 0; i < 200; ++i)
        threads.emplace_back([&]{ v.push_back(42); });
    for (auto& t : threads) t.join();

    CHECK(v.operations_in_progress.load() == 0);
}