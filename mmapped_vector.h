#include <sys/mman.h>
#include <string>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring> // for std::memcpy
#include <mutex>
#include <atomic>
#include <algorithm>


#include "allocators.h"


#define USE_INELEGANT_IMPLEMENTATION 1
//#define MEMORY_ORDER std::memory_order_seq_cst
#define MEMORY_ORDER std::memory_order_relaxed
//#define MEMORY_ORDER std::memory_order_acq_rel

template <typename T>
void atomic_store_max(std::atomic<T>& target, T value) {
    T current = target.load(MEMORY_ORDER);
    while (value > current) {
        if (target.compare_exchange_weak(current, value, MEMORY_ORDER)) {
            break;
        }
    }
}

namespace mmapped_vector {

template <typename T, typename AllocatorType>
class IndexHolder;

template <typename T, typename AllocatorType, bool thread_safe = false>
class MmappedVector {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable for safe memory movement");
    static_assert(std::is_base_of<Allocator<T>, AllocatorType>::value, "AllocatorType must be derived from Allocator");

private:
    AllocatorType allocator;
    std::conditional_t<thread_safe, std::atomic<size_t>, size_t> element_count;
    std::conditional_t<thread_safe, std::atomic<size_t>, std::monostate> capacity_atomic;
    std::conditional_t<thread_safe, std::atomic<size_t>, std::monostate> operations_in_progress;
    std::conditional_t<thread_safe, std::atomic<size_t>, std::monostate> needed_capacity;
    std::conditional_t<thread_safe, std::mutex, std::monostate> mutex;

public:
    // Data type
    using value_type = T;
    using allocator_type = AllocatorType;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;


    // Default constructor: Creates an empty vector with initial capacity
    template <typename... Args>
    MmappedVector(Args&&... args);

    // Destructor: Cleans up resources
    ~MmappedVector();

    // Deleted copy constructor and assignment operator
    MmappedVector(const MmappedVector& other) = delete;
    MmappedVector& operator=(const MmappedVector& other) = delete;

    // Move constructor
    MmappedVector(MmappedVector&& other) noexcept;

    // Move assignment operator
    MmappedVector& operator=(MmappedVector&& other) noexcept;

    // Adds an element to the end of the vector
    void push_back(const T& value);

    // Constructs an element in-place at the end of the vector
    template<typename... Args>
    void emplace_back(Args&&... args);

    // Removes the last element from the vector
    void pop_back();

    // Access element at specified index (no bounds checking)
    T& operator[](size_t index);
    const T& operator[](size_t index) const;

    // Returns the number of elements in the vector
    size_t size() const;

    // Returns the current capacity of the vector
    size_t capacity() const;

    // Checks if the vector is empty
    bool empty() const;

    // Returns a reference to the first element
    T& front();
    const T& front() const;

    // Returns a reference to the last element
    T& back();
    const T& back() const;

    // Removes all elements from the vector
    void clear();

    // Changes the number of elements stored
    void resize(size_t new_size);

    // Requests that the vector capacity be at least enough to contain n elements
    void reserve(size_t new_capacity);

    // Reduces memory usage by freeing unused memory
    void shrink_to_fit();

    // Returns pointer to the underlying array
    T* data();
    const T* data() const;

    // Iterator support
    T* begin();
    T* end();
    const T* begin() const;
    const T* end() const;
    const T* cbegin() const;
    const T* cend() const;

    // Access element at specified index (with bounds checking)
    T& at(size_t pos);
    const T& at(size_t pos) const;

    // Comparison operators
    bool operator==(const MmappedVector& other) const;
    bool operator!=(const MmappedVector& other) const;

    void store_at_index(const T& value, size_t index);

    friend class IndexHolder<T, AllocatorType>;
private:
};

// Method implementations

template <typename T, typename AllocatorType, bool thread_safe>
template <typename... Args>
MmappedVector<T, AllocatorType, thread_safe>::MmappedVector(Args&&... args)
    : allocator(std::forward<Args>(args)...), element_count(allocator.get_backing_size()) {
        if constexpr(thread_safe) {
            capacity_atomic.store(allocator.get_capacity(), MEMORY_ORDER);
            needed_capacity.store(allocator.get_capacity(), MEMORY_ORDER);
            operations_in_progress.store(0, MEMORY_ORDER);
        }
    };


template <typename T, typename AllocatorType, bool thread_safe>
MmappedVector<T, AllocatorType, thread_safe>::MmappedVector(MmappedVector&& other) noexcept
    : allocator(std::move(other.allocator)), element_count(other.element_count) {
    if constexpr(thread_safe) {
        throw std::runtime_error("Not implemented");
    }
    other.element_count = 0;
};

template <typename T, typename AllocatorType, bool thread_safe>
MmappedVector<T, AllocatorType, thread_safe>& MmappedVector<T, AllocatorType, thread_safe>::operator=(MmappedVector&& other) noexcept {
    if constexpr(thread_safe) {
        throw std::runtime_error("Not implemented");
    }
    if (this != &other) {
        allocator = std::move(other.allocator);
        element_count = other.element_count;

        other.element_count = 0;
    }
    return *this;
};

template <typename T, typename AllocatorType, bool thread_safe>
MmappedVector<T, AllocatorType, thread_safe>::~MmappedVector() { allocator.sync(this->element_count); };

template <typename T, typename AllocatorType, bool thread_safe> inline
const T& MmappedVector<T, AllocatorType, thread_safe>::operator[](size_t index) const {
    return allocator.ptr[index];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T& MmappedVector<T, AllocatorType, thread_safe>::operator[](size_t index) {
    return allocator.ptr[index];
};


#if USE_INELEGANT_IMPLEMENTATION
template <typename T, typename AllocatorType, bool thread_safe>
void MmappedVector<T, AllocatorType, thread_safe>::store_at_index(const T& value, size_t index) {
    if constexpr(!thread_safe) {
        throw std::runtime_error("This function should only be called in thread-safe mode");
    }
    operations_in_progress.fetch_add(1, MEMORY_ORDER);
    size_t current_capacity = capacity_atomic.load(MEMORY_ORDER);
    if (index < current_capacity) {
        allocator.ptr[index] = value;
        operations_in_progress.fetch_sub(1, MEMORY_ORDER);
    } else {
        atomic_store_max(needed_capacity, index + 1);
        size_t active_workers = operations_in_progress.fetch_sub(1, MEMORY_ORDER);
        if (active_workers > 1) {
            while (capacity_atomic.load(MEMORY_ORDER) <= index) {};
        } else {
            std::lock_guard<std::mutex> lock(mutex);
            allocator.increase_capacity(std::max(needed_capacity.load(MEMORY_ORDER), index + 1));
            capacity_atomic.store(allocator.get_capacity(), MEMORY_ORDER);
            store_at_index(value, index);
        }
    }
};

#else

template <typename T, typename AllocatorType, bool thread_safe>
inline void MmappedVector<T, AllocatorType, thread_safe>::store_at_index(const T& value, size_t index) {
    if constexpr(!thread_safe) {
        throw std::runtime_error("This function should only be called in thread-safe mode");
    }
    IndexHolder<T, AllocatorType> holder(*this, index);
    allocator.ptr[index] = value;
};

#endif

template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::push_back(const T& value) {
    if constexpr(thread_safe) {
        size_t index = element_count.fetch_add(1, MEMORY_ORDER);
        store_at_index(value, index);
    } else {
        if (element_count >= allocator.get_capacity())
            allocator.increase_capacity(element_count + 1);
        allocator.ptr[element_count++] = value;
    }
};


// TODO: shrink if needed
template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::pop_back() {
    element_count--;
};


template <typename T, typename AllocatorType, bool thread_safe> inline
size_t MmappedVector<T, AllocatorType, thread_safe>::size() const {
    return element_count;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
size_t MmappedVector<T, AllocatorType, thread_safe>::capacity() const {
    return allocator.get_capacity();
};

template <typename T, typename AllocatorType, bool thread_safe> inline
bool MmappedVector<T, AllocatorType, thread_safe>::empty() const {
    return element_count == 0;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T& MmappedVector<T, AllocatorType, thread_safe>::front() {
    return allocator.ptr[0];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T& MmappedVector<T, AllocatorType, thread_safe>::front() const {
    return allocator.ptr[0];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T& MmappedVector<T, AllocatorType, thread_safe>::back() {
    return allocator.ptr[element_count - 1];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T& MmappedVector<T, AllocatorType, thread_safe>::back() const {
    return allocator.ptr[element_count - 1];
};

// TODO shrink if needed
template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::clear() {
    element_count = 0;
};

// TODO probably needs to be deleted
template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::resize(size_t new_size) {
    allocator.resize(new_size);
    element_count = new_size;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::reserve(size_t new_capacity) {
    allocator.increase_capacity(new_capacity);
};

template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::shrink_to_fit() {
    allocator.resize(element_count);
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T* MmappedVector<T, AllocatorType, thread_safe>::data() {
    return allocator.ptr;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T* MmappedVector<T, AllocatorType, thread_safe>::data() const {
    return allocator.ptr;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T* MmappedVector<T, AllocatorType, thread_safe>::begin() {
    return allocator.ptr;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T* MmappedVector<T, AllocatorType, thread_safe>::end() {
    return allocator.ptr + element_count;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T* MmappedVector<T, AllocatorType, thread_safe>::begin() const {
    return allocator.ptr;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T* MmappedVector<T, AllocatorType, thread_safe>::end() const {
    return allocator.ptr + element_count;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T* MmappedVector<T, AllocatorType, thread_safe>::cbegin() const {
    return allocator.ptr;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T* MmappedVector<T, AllocatorType, thread_safe>::cend() const {
    return allocator.ptr + element_count;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
T& MmappedVector<T, AllocatorType, thread_safe>::at(size_t pos) {
    if (pos >= element_count) {
        throw std::out_of_range("MmappedVector::at: index out of range");
    }
    return allocator.ptr[pos];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
const T& MmappedVector<T, AllocatorType, thread_safe>::at(size_t pos) const {
    if (pos >= element_count) {
        throw std::out_of_range("MmappedVector::at: index out of range");
    }
    return allocator[pos];
};

template <typename T, typename AllocatorType, bool thread_safe> inline
bool MmappedVector<T, AllocatorType, thread_safe>::operator==(const MmappedVector& other) const {
    if (element_count != other.element_count) return false;
    for (size_t i = 0; i < element_count; i++) {
        if (allocator.ptr[i] != other.allocator.ptr[i]) return false;
    }
    return true;
};

template <typename T, typename AllocatorType, bool thread_safe> inline
bool MmappedVector<T, AllocatorType, thread_safe>::operator!=(const MmappedVector& other) const {
    return !(*this == other);
};

template <typename T, typename AllocatorType, bool thread_safe>
template<typename... Args> inline
void MmappedVector<T, AllocatorType, thread_safe>::emplace_back(Args&&... args) {
    if constexpr(thread_safe) {
        throw std::runtime_error("Not implemented");
    } else {
        if (element_count >= allocator.get_capacity())
            allocator.increase_capacity(element_count + 1);
        new(&allocator.ptr[element_count++]) T(std::forward<Args>(args)...);
    }
};



template <typename T>
using MallocVector = MmappedVector<T, MallocAllocator<T>>;

template <typename T>
using MmapVector = MmappedVector<T, MmapAllocator<T>>;

template <typename T>
using MmapFileVector = MmappedVector<T, MmapFileAllocator<T>>;



template<typename T, typename AllocatorType>
class IndexHolder {
    MmappedVector<T, AllocatorType, true>& vec;
public:
    inline IndexHolder(MmappedVector<T, AllocatorType, true>& vec, size_t index) : vec(vec) {

        vec.operations_in_progress.fetch_add(1, MEMORY_ORDER);
        size_t current_capacity = vec.capacity_atomic.load(MEMORY_ORDER);
        if (index >= current_capacity)
            slow_path(index);
    }

    inline void slow_path(size_t index) {
        atomic_store_max(vec.needed_capacity, index + 1);
        size_t active_workers = vec.operations_in_progress.fetch_sub(1, MEMORY_ORDER);
        if (active_workers > 1) {
            while (vec.capacity_atomic.load(MEMORY_ORDER) <= index) {};
        } else {
            std::lock_guard<std::mutex> lock(vec.mutex);
            vec.allocator.increase_capacity(std::max(vec.needed_capacity.load(MEMORY_ORDER), index + 1));
            vec.capacity_atomic.store(vec.allocator.get_capacity(), MEMORY_ORDER);
        }
        vec.operations_in_progress.fetch_add(1, MEMORY_ORDER);
    }

    inline ~IndexHolder() {
        vec.operations_in_progress.fetch_sub(1, MEMORY_ORDER);
    }

};

} // namespace mmapped_vector