#include <sys/mman.h>
#include <string>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring> // for std::memcpy
#include <mutex>
#include <atomic>

#include "allocators.h"

namespace mmapped_vector {

template <typename T, typename AllocatorType, bool thread_safe = false>
class MmappedVector {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable for safe memory movement");
    static_assert(std::is_base_of<Allocator<T>, AllocatorType>::value, "AllocatorType must be derived from Allocator");

private:
    AllocatorType allocator;
    std::conditional_t<thread_safe, std::atomic<size_t>, size_t> element_count;

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
    MmappedVector(MmappedVector&& other) noexcept = delete;

    // Move assignment operator
    MmappedVector& operator=(MmappedVector&& other) noexcept = delete;

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

private:
};

// Method implementations

template <typename T, typename AllocatorType, bool thread_safe>
template <typename... Args>
MmappedVector<T, AllocatorType, thread_safe>::MmappedVector(Args&&... args)
    : allocator(std::forward<Args>(args)...), element_count(allocator.get_backing_size()) {};


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


template <typename T, typename AllocatorType, bool thread_safe> inline
void MmappedVector<T, AllocatorType, thread_safe>::push_back(const T& value) {
    if constexpr(thread_safe) {
        throw std::runtime_error("Not implemented");
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

} // namespace mmapped_vector