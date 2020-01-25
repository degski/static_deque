
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
#include <limits>
#include <sax/iostream.hpp>
#include <random>
#include <string>
#include <type_traits>

#include <sax/short_alloc.hpp>

#include <experimental/fixed_capacity_vector>

#pragma once

template<typename T, typename = std::enable_if_t<std::conjunction_v<std::is_integral<T>, std::is_unsigned<T>>>>
constexpr T next_power_2 ( T value ) noexcept {
    value |= ( value >> 1 );
    value |= ( value >> 2 );
    if constexpr ( sizeof ( T ) > 1 ) {
        value |= ( value >> 4 );
    }
    if constexpr ( sizeof ( T ) > 2 ) {
        value |= ( value >> 8 );
    }
    if constexpr ( sizeof ( T ) > 4 ) {
        value |= ( value >> 16 );
    }
    return ++value;
}

template<typename T, typename = std::enable_if_t<std::conjunction_v<std::is_integral<T>, std::is_unsigned<T>>>>
constexpr bool is_power_2 ( T const n_ ) noexcept {
    return n_ and not( n_ & ( n_ - 1 ) );
}

template<std::size_t N /* the total size */, std::size_t alignment = alignof ( std::max_align_t )>
class arena {

    static_assert ( N > 16, "arena: N is too small" );

    alignas ( alignment ) char buf_[ N - 2 * sizeof ( char * ) ];
    char *ptr_, *next_ = nullptr;

    public:
    ~arena ( ) { ptr_ = nullptr; }
    arena ( ) noexcept : ptr_ ( buf_ ) {}

    arena ( const arena & ) = delete;
    arena & operator= ( const arena & ) = delete;

    template<std::size_t ReqAlign>
    char * allocate ( std::size_t n );
    void deallocate ( char * p, std::size_t n ) noexcept;

    static constexpr std::size_t size ( ) noexcept { return N; }
    std::size_t used ( ) const noexcept { return static_cast<std::size_t> ( ptr_ - buf_ ); }
    void reset ( ) noexcept { ptr_ = buf_; }

    private:
    static std::size_t align_up ( std::size_t n ) noexcept { return ( n + ( alignment - 1 ) ) & ~( alignment - 1 ); }

    bool pointer_in_buffer ( char * p ) noexcept { return buf_ <= p && p <= buf_ + N; }
};

template<std::size_t N, std::size_t alignment>
template<std::size_t ReqAlign>
char * arena<N, alignment>::allocate ( std::size_t n ) {
    static_assert ( ReqAlign <= alignment, "alignment is too small for this arena" );
    assert ( pointer_in_buffer ( ptr_ ) && "short_alloc has outlived arena" );
    auto const aligned_n = align_up ( n );
    if ( static_cast<decltype ( aligned_n )> ( buf_ + N - ptr_ ) >= aligned_n ) {
        char * r = ptr_;
        ptr_ += aligned_n;
        return r;
    }
    static_assert ( alignment <= alignof ( std::max_align_t ), "you've chosen an "
                                                               "alignment that is larger than alignof(std::max_align_t), and "
                                                               "cannot be guaranteed by normal operator new" );
    return nullptr;
}

template<std::size_t N, std::size_t alignment>
void arena<N, alignment>::deallocate ( char * p, std::size_t n ) noexcept {
    assert ( pointer_in_buffer ( ptr_ ) && "short_alloc has outlived arena" );
    if ( pointer_in_buffer ( p ) ) {
        n = align_up ( n );
        if ( p + n == ptr_ )
            ptr_ = p;
    }
}

template<class T, std::size_t N, std::size_t Align = alignof ( std::max_align_t )>
class short_alloc {
    public:
    using value_type                             = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::true_type;

    static auto constexpr alignment = Align;
    static auto constexpr size      = N;
    using arena_type                = arena<size, alignment>;

    private:
    arena_type & a_;

    public:
    short_alloc ( const short_alloc & ) = default;
    short_alloc & operator= ( const short_alloc & ) = delete;

    short_alloc ( arena_type & a ) noexcept : a_ ( a ) {
        static_assert ( size % alignment == 0, "size N needs to be a multiple of alignment Align" );
    }
    template<class U>
    short_alloc ( const short_alloc<U, N, alignment> & a ) noexcept : a_ ( a.a_ ) {}

    template<class _Up>
    struct rebind {
        using other = short_alloc<_Up, N, alignment>;
    };

    T * allocate ( std::size_t n ) { return reinterpret_cast<T *> ( a_.template allocate<alignof ( T )> ( n * sizeof ( T ) ) ); }
    void deallocate ( T * p, std::size_t n ) noexcept { a_.deallocate ( reinterpret_cast<char *> ( p ), n * sizeof ( T ) ); }

    template<std::size_t A1, class U, std::size_t M, std::size_t A2>
    friend inline bool operator== ( const sax::short_alloc<T, N, A1> & x, const sax::short_alloc<U, M, A2> & y ) noexcept {
        return N == M && A1 == A2 && &x.a_ == &y.a_;
    }

    template<std::size_t A1, class U, std::size_t M, std::size_t A2>
    friend inline bool operator!= ( const sax::short_alloc<T, N, A1> & x, const sax::short_alloc<U, M, A2> & y ) noexcept {
        return !( x == y );
    }

    template<class U, std::size_t M, std::size_t A>
    friend class short_alloc;
};

template<typename Type, typename SizeType, std::size_t ChunkSize = 512u>
class static_deque {

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

    static constexpr size_type chunck_size = ChunkSize;

    explicit static_deque ( ) noexcept           = default;
    static_deque ( static_deque const & d_ )     = default;
    static_deque ( static_deque && d_ ) noexcept = default;

    [[nodiscard]] static_deque & operator= ( static_deque const & d_ ) = default;
    [[nodiscard]] static_deque & operator= ( static_deque && d_ ) noexcept = default;

    ~static_deque ( ) noexcept = default;

    // Sizes.

    [[nodiscard]] static constexpr size_type max_size ( ) noexcept {
        return std::numeric_limits<size_type>::max ( ) / sizeof ( value_type );
    }

    [[nodiscard]] inline size_type capacity ( ) const noexcept { return 0; }
    [[nodiscard]] inline size_type size ( ) const noexcept { return 0; }

    [[nodiscard]] bool empty ( ) const noexcept { return true; }

    // Output.

    template<typename Stream>
    [[maybe_unused]] friend Stream & operator<< ( Stream & out_, static_deque const & d_ ) noexcept {
        //  if ( d_.data ( ) )
        //     for ( auto const & e : d_ )
        //        out_ << e << sp; // A wide- or narrow-string space, as appropriate.
        return out_;
    }

    private:
    [[maybe_unused]] size_type grow_capacity ( ) noexcept {
        /*
        size_type c = capacity ( );
        if ( c > 1 ) {
            c                = c + c / 2;
            capacity_ref ( ) = c;
            return c;
        }
        else {
            capacity_ref ( ) = 2;
            return 2;
        }
        */
    }

    constexpr pointer allocate ( size_type n_ ) noexcept {}
};
