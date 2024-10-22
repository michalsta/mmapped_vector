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
#include <unistd.h>
#include <cstring>
#include <atomic>
#if false //defined(__APPLE__) && defined(__MACH__)
#include <mach/vm_map.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#endif


#include "error_handling.h"
#include "misc.h"
#include <new>



namespace mmapped_vector {


static const size_t page_size = getpagesize();

template <typename T, typename AllocatorType, bool thread_safe>
class MmappedVector;

template <typename T>
class Allocator
{
protected:
    T* ptr;
    size_t capacity;
public:
    Allocator();
    Allocator(const Allocator&) = delete;
    virtual ~Allocator();
    Allocator& operator=(const Allocator&) = delete;


    virtual T* resize(size_t new_size) = 0;
    void increase_capacity(size_t capacity_needed);
    size_t get_capacity() const;
    T* get_ptr() const;
    virtual size_t get_backing_size() const;
    virtual void sync(size_t used_elements);

    friend class MmappedVector<T, Allocator, false>;
    friend class MmappedVector<T, Allocator, true>;
};


template <typename T> Allocator<T>::Allocator() : ptr(nullptr), capacity(0) {};
template <typename T> Allocator<T>::~Allocator() {};


template <typename T> inline
size_t Allocator<T>::get_capacity() const {
    return this->capacity;
}

template <typename T> inline
T* Allocator<T>::get_ptr() const {
    return this->ptr;
}

template <typename T> inline
size_t Allocator<T>::get_backing_size() const {
    return 0;
}

template <typename T> inline
void Allocator<T>::increase_capacity(size_t capacity_needed) {
    if(this->capacity >= capacity_needed) return;
    size_t new_capacity;
    if(this->capacity <= 8)
        new_capacity = 16;
    else
        new_capacity = this->capacity;
    while(new_capacity < capacity_needed)
        new_capacity *= 2;
    resize(new_capacity);
}


template <typename T> inline
void Allocator<T>::sync(size_t) {};

/*
 * =================================================================================================
 */

template <typename T>
class MmapAllocator : public Allocator<T>
{

public:
    MmapAllocator();
    MmapAllocator(int flags);
    MmapAllocator(const MmapAllocator&) = delete;
    MmapAllocator(MmapAllocator&&) noexcept;
    MmapAllocator& operator=(MmapAllocator&& other) noexcept;
    ~MmapAllocator() override;
    MmapAllocator& operator=(const MmapAllocator&) = delete;

    T* resize(size_t new_size) override;

    friend class MmappedVector<T, MmapAllocator, false>;
    friend class MmappedVector<T, MmapAllocator, true>;
};


template <typename T>
MmapAllocator<T>::MmapAllocator() : MmapAllocator<T>(MAP_ANONYMOUS | MAP_PRIVATE) {};


template <typename T>
MmapAllocator<T>::MmapAllocator(int flags) : Allocator<T>() {
#if  false //defined(__APPLE__) && defined(__MACH__)
    // Use Mach API mach_vm_map to allocate memory
    mach_vm_address_t address = 0;
    kern_return_t kr = mach_vm_map(mach_task_self(), &address, page_size, 0, VM_FLAGS_ANYWHERE, MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) {
        throw std::runtime_error("MmapAllocator::ctor: mach_vm_map failed: " + mmapped_vector::get_error_message("mach_vm_map"));
    }
    this->ptr = reinterpret_cast<T*>(address);
#else
    this->ptr = static_cast<T*>(mmap(nullptr, page_size, PROT_READ | PROT_WRITE, flags, -1, 0));
    if (this->ptr == MAP_FAILED) {
        throw std::runtime_error("MmapAllocator::ctor: mmap failed: " + mmapped_vector::get_error_message("mmap"));
    }
#endif
    this->capacity = page_size / sizeof(T);
}

template <typename T>
MmapAllocator<T>::MmapAllocator(MmapAllocator&& other) noexcept : Allocator<T>() {
    this->ptr = other.ptr;
    this->capacity = other.capacity;
    other.ptr = nullptr;
    other.capacity = 0;
}

template <typename T>
MmapAllocator<T>& MmapAllocator<T>::operator=(MmapAllocator&& other) noexcept {
    if (this != &other) {
        if (this->ptr) {
            munmap(this->ptr, this->capacity * sizeof(T));
        }
        this->ptr = other.ptr;
        this->capacity = other.capacity;
        other.ptr = nullptr;
        other.capacity = 0;
    }
    return *this;
}

template <typename T>
MmapAllocator<T>::~MmapAllocator() {
    if (this->ptr) {
        munmap(this->ptr, this->capacity * sizeof(T));
        this->ptr = nullptr;
        this->capacity = 0;
    }
}

template <typename T>
T* MmapAllocator<T>::resize(size_t new_capacity) {
    if (new_capacity == this->capacity) return this->ptr;

#ifdef MREMAP_MAYMOVE
    void* new_ptr = mremap(this->ptr, this->capacity * sizeof(T), new_capacity * sizeof(T), MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED)
        throw_if_error("mremap");
    this->ptr = static_cast<T*>(new_ptr);

#elif false // defined(__APPLE__) && defined(__MACH__)
    // Use Mach API mach_vm_remap to resize the memory region
    mach_vm_address_t new_address = 0;
    mach_vm_size_t new_size = new_capacity * sizeof(T);
    kern_return_t kr = mach_vm_remap(mach_task_self(), &new_address, new_size, 0, VM_FLAGS_ANYWHERE, mach_task_self(), reinterpret_cast<mach_vm_address_t>(this->ptr), FALSE, nullptr, nullptr, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) {
        throw std::runtime_error("MmapAllocator::resize: mach_vm_remap failed: " + mmapped_vector::get_error_message("mach_vm_remap"));
    }
    this->ptr = reinterpret_cast<T*>(new_address);

#else
    // FIXME: Perhaps Mach API has something that'd allow us to avoid copying the data
    void* new_ptr = mmap(nullptr, new_capacity * sizeof(T), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (new_ptr == MAP_FAILED) {
        throw std::runtime_error("MmapAllocator::resize: mmap failed: " + mmapped_vector::get_error_message("mmap"));
    }
    std::copy(this->ptr, this->ptr + std::min(this->capacity, new_capacity), static_cast<T*>(new_ptr));
    if(munmap(this->ptr, this->capacity * sizeof(T)) == -1)
        throw std::runtime_error("MmapAllocator::resize: munmap failed: " + mmapped_vector::get_error_message("munmap"));
    this->ptr = static_cast<T*>(new_ptr);

#endif

    this->capacity = new_capacity;
    return this->ptr;
}


/*
 * =================================================================================================
 */



template <typename T>
class MmapFileAllocator : public Allocator<T>
{
public:
    MmapFileAllocator(const std::string& file_name, int mmap_flags = MAP_SHARED, int open_flags = O_RDWR | O_CREAT, mode_t mode = S_IRUSR | S_IWUSR);
    MmapFileAllocator(const MmapFileAllocator&) = delete;
    MmapFileAllocator(MmapFileAllocator&&) noexcept;
    MmapFileAllocator& operator=(MmapFileAllocator&&) noexcept;
    ~MmapFileAllocator() override;
    MmapFileAllocator& operator=(const MmapFileAllocator&) = delete;

    T* resize(size_t new_size) override;
    size_t get_backing_size() const override;
    void sync(size_t used_elements) override;

    friend class MmappedVector<T, MmapFileAllocator, false>;
    friend class MmappedVector<T, MmapFileAllocator, true>;
private:
    void self_close() noexcept;
    std::string file_name;
    int file_descriptor;
    size_t backing_size;
};

template <typename T> inline
size_t MmapFileAllocator<T>::get_backing_size() const {
    return this->backing_size;
}

template <typename T>
MmapFileAllocator<T>::MmapFileAllocator(const std::string& file_name, int mmap_flags, int open_flags, mode_t mode) : Allocator<T>() {

    RAIIFileDescriptor fd(open(file_name.c_str(), open_flags, mode));
    if (fd.get() == -1) {
        std::string error_message = "MmapFileAllocator::ctor: " + file_name + ": " + mmapped_vector::get_error_message("open");
        throw std::runtime_error(error_message);
    }

    struct stat st;
    if (fstat(fd.get(), &st) == -1)
        throw std::runtime_error("MmapFileAllocator::ctor: fstat failed: " + mmapped_vector::get_error_message("fstat"));

    if(st.st_size % sizeof(T) != 0)
        throw std::runtime_error("MmapFileAllocator::ctor: file size is not a multiple of sizeof(T). It's probably corrupted.");

    this->backing_size = st.st_size / sizeof(T);
    this->capacity = this->backing_size;
    if(this->capacity < 16)
    {
        this->capacity = 16;
        if(ftruncate(fd.get(), this->capacity * sizeof(T)) == -1)
            throw std::runtime_error("MmapFileAllocator::ctor: ftruncate failed: " + mmapped_vector::get_error_message("ftruncate"));

    }

    this->ptr = static_cast<T*>(mmap(nullptr, this->capacity * sizeof(T), PROT_READ | PROT_WRITE, mmap_flags, fd.get(), 0));
    if (this->ptr == MAP_FAILED)
        throw std::runtime_error("MmapFileAllocator::ctor: mmap failed: " + mmapped_vector::get_error_message("mmap"));

    this->file_name = file_name;
    this->file_descriptor = fd.release();

}

template <typename T>
MmapFileAllocator<T>::MmapFileAllocator(MmapFileAllocator&& other) noexcept : Allocator<T>() {
    this->ptr = other.ptr;
    this->capacity = other.capacity;
    this->backing_size = other.backing_size;
    this->file_name = std::move(other.file_name);
    this->file_descriptor = other.file_descriptor;
    other.ptr = nullptr;
    other.capacity = 0;
    other.backing_size = 0;
    other.file_descriptor = -1;
}

template <typename T>
MmapFileAllocator<T>& MmapFileAllocator<T>::operator=(MmapFileAllocator&& other) noexcept {
    if (this != &other) {
        self_close();
        this->ptr = other.ptr;
        this->capacity = other.capacity;
        this->backing_size = other.backing_size;
        this->file_name = std::move(other.file_name);
        this->file_descriptor = other.file_descriptor;
        other.ptr = nullptr;
        other.capacity = 0;
        other.backing_size = 0;
        other.file_descriptor = -1;
    }
    return *this;
}

template <typename T>
void MmapFileAllocator<T>::self_close() noexcept {
    if (this->ptr) {
        this->resize(this->get_backing_size()); // Truncate the file to the actual size
        munmap(this->ptr, this->get_backing_size() * sizeof(T));
        this->ptr = nullptr;
        this->backing_size = 0;
        this->capacity = 0;
        if (this->file_descriptor != -1) {
            close(this->file_descriptor);
            this->file_descriptor = -1;
        }
    }
}


template <typename T>
MmapFileAllocator<T>::~MmapFileAllocator() {
    self_close();
}

template <typename T>
T* MmapFileAllocator<T>::resize(size_t new_capacity) {
    if (new_capacity == this->capacity) return this->ptr;

    if (ftruncate(this->file_descriptor, new_capacity * sizeof(T)) == -1)
        throw std::runtime_error("MmapFileAllocator::resize: ftruncate failed: " + mmapped_vector::get_error_message("ftruncate"));

#ifdef MREMAP_MAYMOVE
    void* new_ptr = mremap(this->ptr, this->capacity * sizeof(T), new_capacity * sizeof(T), MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED) {
        throw std::runtime_error("MmapFileAllocator: mremap failed: " + mmapped_vector::get_error_message("mremap"));
    }
#else
    if(munmap(this->ptr, this->capacity * sizeof(T)) == -1)
        throw std::runtime_error("MmapFileAllocator::resize: munmap failed: " + mmapped_vector::get_error_message("munmap"));
    void* new_ptr = mmap(nullptr, new_capacity * sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, this->file_descriptor, 0);
    if (new_ptr == MAP_FAILED) {
        throw std::runtime_error("MmapFileAllocator::resize: mmap failed: " + mmapped_vector::get_error_message("mmap"));
    }
#endif

    this->ptr = static_cast<T*>(new_ptr);
    this->capacity = new_capacity;
    return this->ptr;
}

template <typename T>
void MmapFileAllocator<T>::sync(size_t used_elements) {
    this->backing_size = used_elements;
}

/*
 * =================================================================================================
 */


template <typename T>
class MallocAllocator : public Allocator<T>
{
public:
    MallocAllocator();
    MallocAllocator(const MallocAllocator&) = delete;   // Copy constructor is not allowed unless elided
    MallocAllocator(MallocAllocator&&) noexcept;
    MallocAllocator& operator=(MallocAllocator&&) noexcept;
    ~MallocAllocator() override;
    MallocAllocator& operator=(const MallocAllocator&) = delete;

    T* resize(size_t new_size) override;

    friend class MmappedVector<T, MallocAllocator, false>;
    friend class MmappedVector<T, MallocAllocator, true>;
};

template <typename T>
MallocAllocator<T>::MallocAllocator() : Allocator<T>() {
    this->ptr = static_cast<T*>(malloc(16 * sizeof(T)));
    if (!this->ptr) {
        throw std::runtime_error("MallocAllocator: malloc failed");
    }
    this->capacity = 16;

}

template <typename T>
MallocAllocator<T>::MallocAllocator(MallocAllocator&& other) noexcept : Allocator<T>() {
    this->ptr = other.ptr;
    this->capacity = other.capacity;
    other.ptr = nullptr;
    other.capacity = 0;
}

template <typename T>
MallocAllocator<T>& MallocAllocator<T>::operator=(MallocAllocator&& other) noexcept {
    if (this != &other) {
        if (this->ptr) {
            free(this->ptr);
        }
        this->ptr = other.ptr;
        this->capacity = other.capacity;
        other.ptr = nullptr;
        other.capacity = 0;
    }
    return *this;
}

template <typename T>
MallocAllocator<T>::~MallocAllocator() {

    if (this->ptr) {
        free(this->ptr);
        this->ptr = nullptr;
        this->capacity = 0;
    }
}

template <typename T>
T* MallocAllocator<T>::resize(size_t new_capacity) {
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
