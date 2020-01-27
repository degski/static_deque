
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
#include <string>
#include <type_traits>
#include <variant>

#include <sax/splitmix.hpp>
#include <sax/uniform_int_distribution.hpp>

#include <static_deque.hpp>

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

int counter = 0;

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

    private:
    pointer m_raw;

    public:
    unique_ptr ( ) : m_raw ( nullptr ) {}
    // Explicit constructor
    explicit unique_ptr ( pointer raw ) : m_raw ( raw ) {}
    ~unique_ptr ( ) {
        if ( is_unique ( ) )
            delete m_raw;
    }

    // Constructor/Assignment that binds to nullptr
    // This makes usage with nullptr cleaner
    unique_ptr ( std::nullptr_t ) : m_raw ( nullptr ) {}
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
    pointer operator-> ( ) const noexcept { return v ( m_raw ); }
    reference operator* ( ) const { return *v ( m_raw ); }

    // Access to smart pointer state
    pointer get ( ) const noexcept { return v ( m_raw ); }
    explicit operator bool ( ) const { return v ( m_raw ); }

    // Modify object state
    pointer release ( ) noexcept {
        pointer result = nullptr;
        std::swap ( result, m_raw );
        return v ( result );
    }
    void swap ( unique_ptr & src ) noexcept { std::swap ( m_raw, src.m_raw ); }

    void reset ( ) noexcept {
        pointer tmp = release ( );
        delete v ( tmp );
    }
    void reset ( pointer ptr = pointer ( ) ) noexcept {
        pointer result = ptr;
        std::swap ( result, m_raw );
        delete v ( result );
    }
    template<typename U>
    void reset ( unique_ptr<U> && moving ) noexcept {
        unique_ptr<T> result ( moving );
        std::swap ( result, *this );
        delete v ( result );
    }

    void make_weak ( ) noexcept { m_raw |= one_mask; }
    void make_unique ( ) noexcept { m_raw &= ptr_mask; }

    void swap_ownership ( unique_ptr & other_ ) noexcept {
        assert ( v ( m_raw ) == v ( other_.m_raw ) );
        make_weak ( );
        other_.make_weak ( );
    }

    [[nodiscard]] bool is_weak ( ) const noexcept {
        return static_cast<bool> ( reinterpret_cast<std::uintptr_t> ( m_raw ) & one_mask );
    }
    [[nodiscard]] bool is_unique ( ) const noexcept { return not is_weak ( ); }

    [[nodiscard]] static constexpr pointer pointer_view ( pointer p_ ) noexcept {
        return reinterpret_cast<pointer> ( reinterpret_cast<std::uintptr_t> ( p_ ) | ptr_mask );
    }
    [[nodiscard]] static constexpr pointer v ( pointer p_ ) noexcept { return pointer_view ( p_ ); }

    private:
    static constexpr std::uintptr_t ptr_mask = 0x00FF'FFFF'FFFF'FFF0;
    static constexpr std::uintptr_t one_mask = 0x0000'0000'0000'0001;
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

template<std::size_t N, std::align_val_t Align>
struct aligned_stack_storage {

    explicit constexpr aligned_stack_storage ( ) noexcept : m_next ( this ) {
        std::cout << "c'tor " << c << " called" << nl;
        // An object cannot own itself.
        m_next.make_weak ( );
    };
    aligned_stack_storage ( aligned_stack_storage const & )     = delete;
    aligned_stack_storage ( aligned_stack_storage && ) noexcept = delete;

    template<typename U>
    explicit constexpr aligned_stack_storage ( unique_ptr<U> && p_ ) noexcept : m_next ( p_ ) {
        std::cout << "c'tor (m_next moved in) " << c << " called" << nl;
    };

    ~aligned_stack_storage ( ) noexcept { std::cout << "d'tor " << c << " called" << nl; }

    aligned_stack_storage & operator= ( aligned_stack_storage const & ) = delete;
    aligned_stack_storage & operator= ( aligned_stack_storage && ) = delete;

    static constexpr std::size_t capacity ( ) noexcept { return char_size; };

    static constexpr std::size_t char_size = N - 2 * sizeof ( char * );

    alignas ( static_cast<std::size_t> ( Align ) ) char m_storage[ char_size ];
    unique_ptr<aligned_stack_storage> m_next;
    int c = counter++;
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

    using aligned_stack_storage = aligned_stack_storage<ChunkSize, static_cast<std::align_val_t> ( alignof ( value_type ) )>;
    using unique_ptr            = unique_ptr<aligned_stack_storage>;

    static constexpr size_type chunck_size = static_cast<size_type> ( aligned_stack_storage::capacity ( ) / sizeof ( value_type ) );

    unique_ptr m_last_data;

    [[nodiscard]] static constexpr unique_ptr chk_allocate ( ) noexcept { return make_unique<aligned_stack_storage> ( ); }
    [[nodiscard]] static constexpr unique_ptr chk_allocate ( unique_ptr && p_ ) noexcept {
        return make_unique<aligned_stack_storage> ( std::move ( p_ ) );
    }

    // Swap the containing type, between 2 unique_ptr's, pointing at the same value.
    void swap_types ( unique_ptr & p0_, unique_ptr & p1_ ) const noexcept { p0_.swap_ownership ( p1_ ); }

    void grow ( ) noexcept {

        if ( m_last_data ) {
            // std::get<unique_ptr> ( m_last_data )->m_next = chk_allocate (
            //    std::get<unique_ptr> ( m_last_data )->m_next ); // Constuct new aligned_stack_storage, and move into m_next.
            // swap_types ( chk_leaf ( ), m_last_data );
            // Move last forward.
        }
        else {
            // m_last_data = chk_allocate ( );
        }
    }

    pointer m_front = nullptr, m_back = nullptr;
};

int main657565 ( ) {

    std::exception_ptr eptr;

    try {
        unique_ptr<int> p;
    }
    catch ( ... ) {
        eptr = std::current_exception ( ); // Capture.
    }
    handleEptr ( eptr );

    return EXIT_SUCCESS;
}

int main ( ) {

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
