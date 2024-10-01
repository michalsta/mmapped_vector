/**
 * @file allocators.h
 * @brief File containing the Allocator class and its specializations.
 * @author Michał Startek
 * @version 0.1
 * @copyright Copyright (c) Michał Startek 2024
 */

#ifndef MMAPPED_VECTOR_ALLOCATORS_H
#define MMAPPED_VECTOR_ALLOCATORS_H

#include <cstddef>
#include <string>
#include <sys/mman.h>
#include <mutex>
#include <variant>
#include <fcntl.h>
#include <sys/stat.h>

#include "error_handling.h"
#include "misc.h"



namespace mmapped_vector {

template <typename T, typename AllocatorType, bool thread_safe>
class MmappedVector;

template <typename T, bool thread_safe = false>
class Allocator
{
protected:
    T* ptr;
    size_t capacity;
    std::conditional_t<thread_safe, std::mutex, std::monostate> guard;
public:
    Allocator();
    virtual ~Allocator();

    virtual T* resize_unguarded(size_t new_size) = 0;
    T* resize(size_t new_size);
    void increase_capacity_unguarded(size_t capacity_needed);
    void increase_capacity(size_t capacity_needed);
    size_t get_capacity() const;
    T* get_ptr() const;
    size_t get_backing_size() const;
    virtual void sync(size_t used_elements) const;
    
    friend class MmappedVector<T, Allocator, thread_safe>; // Friend declaration
};

template <typename T, bool thread_safe> Allocator<T, thread_safe>::Allocator() : ptr(nullptr), capacity(0), guard() {};
template <typename T, bool thread_safe> Allocator<T, thread_safe>::~Allocator() {};


template <typename T, bool thread_safe> inline
size_t Allocator<T, thread_safe>::get_capacity() const {
    return this->capacity;
}

template <typename T, bool thread_safe> inline
T* Allocator<T, thread_safe>::get_ptr() const {
    return this->ptr;
}

template <typename T, bool thread_safe> inline
size_t Allocator<T, thread_safe>::get_backing_size() const {
    return 0;
}

template <typename T, bool thread_safe> inline
T* Allocator<T, thread_safe>::resize(size_t new_size) {
    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        return resize_unguarded(new_size);
    }
    else
        return resize_unguarded(new_size);
}

template <typename T, bool thread_safe> inline
void Allocator<T, thread_safe>::increase_capacity_unguarded(size_t capacity_needed) {
    if(this->capacity >= capacity_needed) return;
    size_t new_capacity;
    if(this->capacity <= 8)
        new_capacity = 16;
    else
        new_capacity = this->capacity;
    while(new_capacity < capacity_needed)
        new_capacity *= 2;
    resize_unguarded(new_capacity);
}

template <typename T, bool thread_safe> inline
void Allocator<T, thread_safe>::increase_capacity(size_t capacity_needed) {
    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        increase_capacity_unguarded(capacity_needed);
    }
    else
        increase_capacity_unguarded(capacity_needed);
}

template <typename T, bool thread_safe> inline
void Allocator<T, thread_safe>::sync(size_t) const {};

/*
 * =================================================================================================
 */

template <typename T, bool thread_safe = false>
class MmapAllocator : public Allocator<T, thread_safe>
{
public:
    MmapAllocator();
    MmapAllocator(int flags);
    ~MmapAllocator() override;

    T* resize_unguarded(size_t new_size) override;

    friend class MmappedVector<T, MmapAllocator, thread_safe>; // Friend declaration
};



template <typename T, bool thread_safe>
MmapAllocator<T, thread_safe>::MmapAllocator() : MmapAllocator<T, thread_safe>(MAP_ANONYMOUS | MAP_PRIVATE) {};


template <typename T, bool thread_safe>
MmapAllocator<T, thread_safe>::MmapAllocator(int flags) : Allocator<T, thread_safe>() {
    auto fun_body = [&]()
    {
        this->ptr = static_cast<T*>(mmap(nullptr, 16 * sizeof(T), PROT_READ | PROT_WRITE, flags, -1, 0));
        if (this->ptr == MAP_FAILED) {
            throw std::runtime_error("MmapAllocator: mmap failed: " + mmapped_vector::get_error_message("mmap"));
        }
        this->capacity = 16;
    };

    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        fun_body();
    }
    else
        fun_body();
}

template <typename T, bool thread_safe>
MmapAllocator<T, thread_safe>::~MmapAllocator<T, thread_safe>() {
    auto fun_body = [&]()
    {
        if (this->ptr) {
            munmap(this->ptr, this->size * sizeof(T));
            this->ptr = nullptr;
            this->capacity = 0;
        }
    };
    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        fun_body();
    }
    else
        fun_body();
}

template <typename T, bool thread_safe>
T* MmapAllocator<T, thread_safe>::resize_unguarded(size_t new_capacity) {
    if (new_capacity == this->capacity) return this->ptr;

    void* new_ptr = mremap(this->ptr, this->capacity * sizeof(T), new_capacity * sizeof(T), MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED)
        throw_if_error("mremap");

    this->ptr = static_cast<T*>(new_ptr);
    this->capacity = new_capacity;
    return this->ptr;
}


/*
 * =================================================================================================
 */



template <typename T, bool thread_safe = false>
class MmapFileAllocator : public Allocator<T, thread_safe>
{
public:
    MmapFileAllocator();
    MmapFileAllocator(const std::string& file_name, int mmap_flags, int open_flags = O_RDWR | O_CREAT, mode_t mode = S_IRUSR | S_IWUSR);
    ~MmapFileAllocator() override;

    T* resize_unguarded(size_t new_size) override;
    size_t get_backing_size() const override;
    void sync(size_t used_elements) const override;

    friend class MmappedVector<T, MmapFileAllocator, thread_safe>; // Friend declaration
private:
    std::string file_name;
    int file_descriptor;
    size_t backing_size;
};

template <typename T, bool thread_safe> inline
size_t MmapFileAllocator<T, thread_safe>::get_backing_size() const {
    return this->backing_size;
}

template <typename T, bool thread_safe>
MmapFileAllocator<T, thread_safe>::MmapFileAllocator() : MmapFileAllocator<T, thread_safe>("", MAP_SHARED, O_RDWR | O_CREAT) {};

template <typename T, bool thread_safe>
MmapFileAllocator<T, thread_safe>::MmapFileAllocator(const std::string& file_name, int mmap_flags, int open_flags, mode_t mode) : Allocator<T, thread_safe>() {
    auto fun_body = [&]()
    {
        RAIIFileDescriptor fd(open(file_name.c_str(), open_flags, mode));
        if (fd.get() == -1) {
            throw std::runtime_error("MmapFileAllocator: open failed: " + mmapped_vector::get_error_message("open"));
        }

        struct stat st;
        if (fstat(fd.get(), &st) == -1) {
            throw std::runtime_error("MmapFileAllocator: fstat failed: " + mmapped_vector::get_error_message("fstat"));
        }

        this->ptr = static_cast<T*>(mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, mmap_flags, fd.get(), 0));
        if (this->ptr == MAP_FAILED) {
            throw std::runtime_error("MmapFileAllocator: mmap failed: " + mmapped_vector::get_error_message("mmap"));
        }

        this->backing_size = st.st_size / sizeof(T);
        this->capacity = this->backing_size;
        this->file_name = file_name;
        this->file_descriptor = fd.get();
    };

    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        fun_body();
    }
    else
        fun_body();
}

template <typename T, bool thread_safe>
MmapFileAllocator<T, thread_safe>::~MmapFileAllocator() {
    auto fun_body = [&]()
    {
        if (this->ptr) {
            this->resize_unguarded(this->get_backing_size());
            munmap(this->ptr, this->size * sizeof(T));
            this->ptr = nullptr;
            this->size = 0;
            this->capacity = 0;
            if (this->file_descriptor != -1) {
                close(this->file_descriptor);
                this->file_descriptor = -1;
            }            
        }
    };
    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        fun_body();
    }
    else
        fun_body();
}

template <typename T, bool thread_safe>
T* MmapFileAllocator<T, thread_safe>::resize_unguarded(size_t new_capacity) {
    if (new_capacity == this->capacity) return this->ptr;

    if (ftruncate(this->file_descriptor, new_capacity * sizeof(T)) == -1) {
        throw std::runtime_error("MmapFileAllocator: ftruncate failed: " + mmapped_vector::get_error_message("ftruncate"));
    }

    void* new_ptr = mremap(this->ptr, this->capacity * sizeof(T), new_capacity * sizeof(T), MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED) {
        throw std::runtime_error("MmapFileAllocator: mremap failed: " + mmapped_vector::get_error_message("mremap"));
    }

    this->ptr = static_cast<T*>(new_ptr);
    this->capacity = new_capacity;
    return this->ptr;
}

template <typename T, bool thread_safe>
void MmapFileAllocator<T, thread_safe>::sync(size_t used_elements) const {
    this->backing_size = used_elements;
}

/*
 * =================================================================================================
 */


template <typename T, bool thread_safe = false>
class MallocAllocator : public Allocator<T, thread_safe>
{
public:
    MallocAllocator();
    ~MallocAllocator() override;

    T* resize_unguarded(size_t new_size) override;

    friend class MmappedVector<T, MallocAllocator, thread_safe>; // Friend declaration
};

template <typename T, bool thread_safe>
MallocAllocator<T, thread_safe>::MallocAllocator() : Allocator<T, thread_safe>() {
    auto fun_body = [&]()
    {
        this->ptr = static_cast<T*>(malloc(16 * sizeof(T)));
        if (!this->ptr) {
            throw std::runtime_error("MallocAllocator: malloc failed");
        }
        this->capacity = 16;
    };

    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        fun_body();
    }
    else
        fun_body();
}

template <typename T, bool thread_safe>
MallocAllocator<T, thread_safe>::~MallocAllocator() {
    auto fun_body = [&]()
    {
        if (this->ptr) {
            free(this->ptr);
            this->ptr = nullptr;
            this->capacity = 0;
        }
    };
    if constexpr(thread_safe)
    {
        std::lock_guard<std::mutex>(this->guard);
        fun_body();
    }
    else
        fun_body();
}

template <typename T, bool thread_safe>
T* MallocAllocator<T, thread_safe>::resize_unguarded(size_t new_capacity) {
    if (new_capacity == this->capacity) return this->ptr;

    void* new_ptr = realloc(this->ptr, new_capacity * sizeof(T));
    if (!new_ptr) {
        throw std::runtime_error("MallocAllocator: realloc failed");
    }

    this->ptr = static_cast<T*>(new_ptr);
    this->capacity = new_capacity;
    return this->ptr;
}

} // namespace mmaped_vector

#endif // MMAPPED_VECTOR_ALLOCATORS_H
