
// MIT License
//
// Copyright (c) 2020 degski
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <array>
#include <memory>
#include <sax/iostream.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include <sax/splitmix.hpp>
#include <sax/uniform_int_distribution.hpp>

#include <static_deque.hpp>

#include "trie.h"

// -fsanitize=address
/*
C:\Program Files\LLVM\lib\clang\10.0.0\lib\windows\clang_rt.asan_cxx-x86_64.lib;
C:\Program Files\LLVM\lib\clang\10.0.0\lib\windows\clang_rt.asan-preinit-x86_64.lib;
C:\Program Files\LLVM\lib\clang\10.0.0\lib\windows\clang_rt.asan-x86_64.lib
*/

void handleEptr ( std::exception_ptr eptr ) { // Passing by value is ok.
    try {
        if ( eptr )
            std::rethrow_exception ( eptr );
    }
    catch ( std::exception const & e ) {
        std::cout << "Caught exception \"" << e.what ( ) << "\"\n";
    }
}

template<typename T>
class unique_ptr {

    // https://lokiastari.com/blog/2014/12/30/c-plus-plus-by-example-smart-pointer/
    // https://codereview.stackexchange.com/questions/163854/my-implementation-for-stdunique-ptr

    public:
    using value_type    = T;
    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;

    explicit unique_ptr ( ) : m_data ( nullptr ) {}
    // Explicit constructor
    explicit unique_ptr ( pointer raw ) : m_data ( raw ) {}
    ~unique_ptr ( ) {
        if ( is_unique ( ) )
            delete m_data;
    }

    // Constructor/Assignment that binds to nullptr
    // This makes usage with nullptr cleaner
    unique_ptr ( std::nullptr_t ) : m_data ( nullptr ) {}
    unique_ptr & operator= ( std::nullptr_t ) {
        reset ( );
        return *this;
    }

    // Constructor/Assignment that allows move semantics
    unique_ptr ( unique_ptr && moving ) noexcept { moving.swap ( *this ); }
    unique_ptr & operator= ( unique_ptr && moving ) noexcept {
        moving.swap ( *this );
        return *this;
    }

    // Constructor/Assignment for use with types derived from T
    template<typename U>
    explicit unique_ptr ( unique_ptr<U> && moving ) noexcept {
        unique_ptr<T> tmp ( moving.release ( ) );
        tmp.swap ( *this );
    }
    template<typename U>
    unique_ptr & operator= ( unique_ptr<U> && moving ) noexcept {
        unique_ptr<T> tmp ( moving.release ( ) );
        tmp.swap ( *this );
        return *this;
    }

    // Remove compiler generated copy semantics.
    unique_ptr ( unique_ptr const & ) = delete;
    unique_ptr & operator= ( unique_ptr const & ) = delete;

    // Const correct access owned object
    pointer operator-> ( ) const noexcept { return pointer_view ( m_data ); }
    reference operator* ( ) const { return *pointer_view ( m_data ); }

    // Access to smart pointer state
    pointer get ( ) const noexcept { return pointer_view ( m_data ); }
    pointer get ( ) noexcept { return pointer_view ( m_data ); }
    explicit operator bool ( ) const { return pointer_view ( m_data ); }

    // Modify object state
    pointer release ( ) noexcept {
        pointer result = nullptr;
        std::swap ( result, m_data );
        return pointer_view ( result );
    }
    void swap ( unique_ptr & src ) noexcept { std::swap ( m_data, src.m_data ); }

    void reset ( ) noexcept {
        pointer tmp = release ( );
        delete pointer_view ( tmp );
    }
    void reset ( pointer ptr_ = pointer ( ) ) noexcept {
        pointer result = ptr_;
        std::swap ( result, m_data );
        delete pointer_view ( result );
    }
    template<typename U>
    void reset ( unique_ptr<U> && moving_ ) noexcept {
        unique_ptr<T> result ( moving_ );
        std::swap ( result, *this );
        delete pointer_view ( result );
    }

    void weakify ( ) noexcept {
        m_data = reinterpret_cast<pointer> ( reinterpret_cast<std::uintptr_t> ( pointer_view ( m_data ) ) | weak_mask );
    }
    void uniquify ( ) noexcept { m_data &= ptr_mask; }

    void swap_ownership ( unique_ptr & other_ ) noexcept {
        auto flip = [] ( unique_ptr & u ) {
            u.m_data = reinterpret_cast<pointer> ( reinterpret_cast<std::uintptr_t> ( u.m_data ) | weak_mask );
        };
        flip ( *this );
        flip ( other_ );
    }

    [[nodiscard]] bool is_weak ( ) const noexcept {
        return static_cast<bool> ( reinterpret_cast<std::uintptr_t> ( m_data ) & weak_mask );
    }
    [[nodiscard]] bool is_unique ( ) const noexcept { return not is_weak ( ); }

    [[nodiscard]] static constexpr pointer pointer_view ( pointer p_ ) noexcept {
        return reinterpret_cast<pointer> ( reinterpret_cast<std::uintptr_t> ( p_ ) & ptr_mask );
    }

    private:
    pointer m_data;

    static constexpr std::uintptr_t ptr_mask  = 0x00FF'FFFF'FFFF'FFF0;
    static constexpr std::uintptr_t weak_mask = 0x0000'0000'0000'0001;
};

////////////////////////////////////////////////////////////////////////////////

namespace std {
template<typename T>
void swap ( unique_ptr<T> & lhs, unique_ptr<T> & rhs ) {
    lhs.swap ( rhs );
}
} // namespace std

////////////////////////////////////////////////////////////////////////////////
//
// Stephan T Lavavej (STL!) implementation of make_unique, which has been
// accepted into the C++14 standard. It includes handling for arrays. Paper
// here: http://isocpp.org/files/papers/N3656.txt
//
////////////////////////////////////////////////////////////////////////////////

namespace detail {
template<class T>
struct _Unique_if {
    typedef unique_ptr<T> _Single_object;
};
// Specialization for unbound array.
template<class T>
struct _Unique_if<T[]> {
    typedef unique_ptr<T[]> _Unknown_bound;
};
// Specialization for array of known size.
template<class T, size_t N>
struct _Unique_if<T[ N ]> {
    typedef void _Known_bound;
};
} // namespace detail

////////////////////////////////////////////////////////////////////////////////

// Specialization for normal object type.
template<class T, class... Args>
typename detail::_Unique_if<T>::_Single_object make_unique ( Args &&... args ) {
    return unique_ptr<T> ( new T ( std::forward<Args> ( args )... ) );
}
// Specialization for unknown bound.
template<class T>
typename detail::_Unique_if<T>::_Unknown_bound make_unique ( size_t size ) {
    typedef typename std::remove_extent<T>::type U;
    return unique_ptr<T> ( new U[ size ]( ) );
}
// Deleted specialization.
template<class T, class... Args>
typename detail::_Unique_if<T>::_Known_bound make_unique ( Args &&... ) = delete;

////////////////////////////////////////////////////////////////////////////////

template<class T>
unique_ptr<T> make_unique_default_init ( ) {
    return make_unique<T> ( );
}
template<class T>
unique_ptr<T> make_unique_default_init ( std::size_t size ) {
    return make_unique<T> ( size );
}
template<class T, class... Args>
typename detail::_Unique_if<T>::_Known_bound make_unique_default_init ( Args &&... ) = delete;

////////////////////////////////////////////////////////////////////////////////

template<typename T>
struct params {
    unique_ptr<T> m_next;
    T * m_prev            = nullptr;
    std::uint16_t m_begin = 0, m_end = 0;
    explicit params ( T * moving ) noexcept : m_next ( moving ) {
        m_next.weakify ( );
        assert ( m_next.is_weak ( ) );
    }
    explicit params ( unique_ptr<T> && moving ) noexcept : m_next ( std::move ( moving ) ) { assert ( m_next.is_weak ( ) ); }
};

////////////////////////////////////////////////////////////////////////////////

template<std::size_t N, std::align_val_t Align>
struct aligned_stack_storage {

    explicit constexpr aligned_stack_storage ( ) noexcept : m_next ( this ) {
        std::cout << "c'tor called" << nl;
        // An object cannot own itself.
        m_next.weakify ( );
        assert ( m_next.is_weak ( ) );
    };
    aligned_stack_storage ( aligned_stack_storage const & )     = delete;
    aligned_stack_storage ( aligned_stack_storage && ) noexcept = delete;

    template<typename U>
    explicit constexpr aligned_stack_storage ( unique_ptr<U> && p_ ) noexcept : m_next ( std::move ( p_ ) ) {
        std::cout << "c'tor (m_next moved in) called" << nl;
        assert ( m_next.is_weak ( ) );
    };

    ~aligned_stack_storage ( ) noexcept { std::cout << "d'tor called" << nl; }

    aligned_stack_storage & operator= ( aligned_stack_storage const & ) = delete;
    aligned_stack_storage & operator= ( aligned_stack_storage && ) = delete;

    static constexpr std::size_t capacity ( ) noexcept { return char_size; };

    static constexpr std::size_t char_size = N - 2 * sizeof ( char * );

    alignas ( static_cast<std::size_t> ( Align ) ) char m_storage[ char_size ];
    unique_ptr<aligned_stack_storage> m_next;
};

template<typename Type, typename SizeType, std::size_t ChunkSize = 512u>
class mempool {

    static_assert ( is_power_2 ( ChunkSize ), "Template parameter 3 must be an integral value with a value a power of 2" );

    public:
    using value_type    = Type;
    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    using size_type       = SizeType;
    using difference_type = std::make_signed<size_type>;

    using iterator               = pointer;
    using const_iterator         = const_pointer;
    using reverse_iterator       = pointer;
    using const_reverse_iterator = const_pointer;

    using void_ptr = void *;
    using char_ptr = char *;

    using aligned_stack_storage     = aligned_stack_storage<ChunkSize, static_cast<std::align_val_t> ( alignof ( value_type ) )>;
    using aligned_stack_storage_ptr = aligned_stack_storage *;
    using unique_ptr                = unique_ptr<aligned_stack_storage>;

    static constexpr size_type chunck_size = static_cast<size_type> ( aligned_stack_storage::capacity ( ) / sizeof ( value_type ) );

    unique_ptr m_last_data;

    [[nodiscard]] static constexpr unique_ptr allocate ( ) noexcept { return make_unique<aligned_stack_storage> ( ); }
    [[nodiscard]] static constexpr unique_ptr allocate ( unique_ptr && p_ ) noexcept {
        return make_unique<aligned_stack_storage> ( std::move ( p_ ) );
    }

    [[nodiscard]] unique_ptr & back_next ( unique_ptr const & ptr_ ) const noexcept {
        aligned_stack_storage_ptr n = ptr_.get ( );
        while ( n->m_next.is_unique ( ) )
            n = n->m_next.get ( );
        return n->m_next;
    }
    [[nodiscard]] unique_ptr & back_next ( ) const noexcept { return back_next ( m_last_data ); }

    void grow ( ) noexcept {

        if ( m_last_data.get ( ) ) {
            std::cout << "add" << nl;
            auto & leaf = back_next ( );
            leaf        = mempool::allocate ( std::move ( leaf ) );
        }
        else {
            std::cout << "first" << nl;
            m_last_data = mempool::allocate ( );
        }
    }

    pointer m_front = nullptr, m_back = nullptr;
};

template<typename T>
struct offset_ptr {
    public:
    using value_type    = T;
    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    using size_type   = std::uint32_t;
    using offset_type = std::uint16_t;

    // Constructors.

    explicit offset_ptr ( ) noexcept : offset ( base.incr_ref_count ( 0u ) ) {}

    explicit offset_ptr ( std::nullptr_t ) : offset ( base.incr_ref_count ( 0u ) ) {}

    explicit offset_ptr ( offset_ptr const & ) noexcept = delete;

    offset_ptr ( offset_ptr && moving ) noexcept { moving.swap ( *this ); }

    template<typename U>
    explicit offset_ptr ( offset_ptr<U> && moving ) noexcept {
        offset_ptr<T> tmp ( moving.release ( ) );
        tmp.swap ( *this );
    }

    offset_ptr ( pointer p_ ) noexcept : offset ( base.incr_ref_count ( p_ - offset_ptr::base.ptr ) ) { assert ( get ( ) == p_ ); }

    // Destruct.

    ~offset_ptr ( ) noexcept {
        base.decr_ref_count ( );
        if ( is_unique ( ) )
            delete get ( );
    }

    // Assignment.

    [[maybe_unused]] offset_ptr & operator= ( std::nullptr_t ) {
        incr_ref_count ( );
        reset ( );
        return *this;
    }

    [[maybe_unused]] offset_ptr & operator= ( offset_ptr const & ) noexcept = delete;

    [[maybe_unused]] offset_ptr & operator= ( offset_ptr && moving ) noexcept {
        moving.swap ( *this );
        return *this;
    }

    template<typename U>
    [[maybe_unused]] offset_ptr & operator= ( offset_ptr<U> && moving ) noexcept {
        offset_ptr<T> tmp ( moving.release ( ) );
        tmp.swap ( *this );
        return *this;
    }

    [[maybe_unused]] offset_ptr & operator= ( pointer p_ ) noexcept {
        offset = p_ - offset_ptr::base.ptr;
        assert ( get ( ) == p_ );
        return *this;
    }

    // Get.

    [[nodiscard]] const_pointer operator-> ( ) const noexcept { return get ( ); }
    [[nodiscard]] pointer operator-> ( ) noexcept { return const_cast<pointer> ( std::as_const ( *this ).get ( ) ); }

    [[nodiscard]] const_reference operator* ( ) const noexcept { return *( this->operator-> ( ) ); }
    [[nodiscard]] reference operator* ( ) noexcept { return *( this->operator-> ( ) ); }

    [[nodiscard]] pointer get ( ) const noexcept { return offset_ptr::base.ptr + offset_view ( offset ); }
    [[nodiscard]] pointer get ( offset_type o_ ) const noexcept { return offset_ptr::base + o_; }

    [[nodiscard]] size_type max_size ( ) noexcept {
        return static_cast<size_type> ( std::numeric_limits<offset_type>::max ( ) ) >> 1;
    }

    void swap ( offset_ptr & src ) noexcept { std::swap ( offset, src.offset ); }

    // Other.

    [[nodiscard]] pointer release ( ) noexcept {
        offset_type result = { };
        std::swap ( result, offset );
        return get ( result );
    }

    void reset ( ) noexcept { delete release ( ); }
    void reset ( pointer p_ = pointer ( ) ) noexcept {
        offset_type result = p_ - offset_ptr::base.ptr;
        std::swap ( result, offset );
        delete get ( result );
    }
    template<typename U>
    void reset ( unique_ptr<U> && moving_ ) noexcept {
        offset_ptr<T> result ( moving_ );
        std::swap ( result, *this );
        delete result.get ( );
    }

    void weakify ( ) noexcept { offset = ( offset_view ( offset ) | weak_mask ); }
    void uniquify ( ) noexcept { offset &= offset_mask; }

    [[nodiscard]] bool is_weak ( ) const noexcept { return static_cast<bool> ( offset & weak_mask ); }
    [[nodiscard]] bool is_unique ( ) const noexcept { return not is_weak ( ); }

    [[nodiscard]] static constexpr offset_type offset_view ( offset_type o_ ) noexcept { return o_ & offset_mask; }

    // Deal with heap allocation.

    void * operator new ( std::size_t ) { throw std::bad_alloc ( "offset_ptr cannot be allocated on the heap" ); }
    void operator delete ( void * ) {
        throw std::bad_alloc ( "offset_ptr cannot be allocated on the heap, and does nt have to be deleted" );
    }

    private:
    [[nodiscard]] static constexpr offset_type make_weak_mask ( ) noexcept {
        return static_cast<offset_type> ( std::uint64_t ( 1 ) << ( sizeof ( size_type ) * 8 - 1 ) );
    }
    [[nodiscard]] static constexpr offset_type make_offset_mask ( ) noexcept { return ~make_weak_mask ( ); }

    static constexpr offset_type weak_mask   = offset_ptr::make_weak_mask ( );
    static constexpr offset_type offset_mask = offset_ptr::make_offset_mask ( );

    struct offset_base {

        pointer ptr             = nullptr;
        std::intptr_t ref_count = 0;

        public:
        template<typename U>
        [[maybe_unused]] U && incr_ref_count ( U && o_ ) noexcept {
            if ( ref_count ) {
                ++ref_count;
            }
            else {
                ptr       = stack_top ( );
                ref_count = 1;
            }
            return std::move ( o_ );
        }
        void incr_ref_count ( ) noexcept { incr_ref_count ( 0u ); }
        void decr_ref_count ( ) noexcept { --ref_count; }

        [[nodiscard]] static pointer stack_top ( ) noexcept {
            volatile void * p = std::addressof ( p );
            return reinterpret_cast<pointer> ( const_cast<void *> ( p ) );
        }
    };

    offset_type offset = { };

    static thread_local offset_base base;
};

template<typename T>
thread_local typename offset_ptr<T>::offset_base offset_ptr<T>::base;

int main ( ) {

    offset_ptr<int> p;
    int i = 999;
    p     = &i;

    std::cout << p.get ( ) << nl;

    exit ( 0 );

    mempool<int, std::size_t, 64> pool;

    std::exception_ptr eptr;

    try {

        { pool.grow ( ); }

        std::cout << "leaving scope 1" << nl;

        { pool.grow ( ); }

        std::cout << "leaving scope 2" << nl;

        { pool.grow ( ); }

        std::cout << "leaving scope 3" << nl;
    }
    catch ( ... ) {
        eptr = std::current_exception ( ); // Capture.
    }
    handleEptr ( eptr );

    std::cout << "leaving scope 4" << nl;

    return EXIT_SUCCESS;
}

int main86766 ( ) {

    std::exception_ptr eptr;

    try {
    }
    catch ( ... ) {
        eptr = std::current_exception ( ); // Capture.
    }
    handleEptr ( eptr );

    return EXIT_SUCCESS;
}
