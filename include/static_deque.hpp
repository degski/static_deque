
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

template<std::size_t Size, std::size_t Align = alignof ( std::max_align_t )>
struct aligned_stack_storage_ {

    alignas ( Align ) char m_storage[ Size ];

    public:
    aligned_stack_storage_ ( aligned_stack_storage_ const & ) = delete;
    aligned_stack_storage_ & operator= ( aligned_stack_storage_ const & ) = delete;

    [[nodiscard]] char * allocate ( std::size_t n ) noexcept { return m_storage; };
    void deallocate ( char * p, std::size_t n ) noexcept { assert ( pointer_in_buffer ( p ) ); }

    private:
    [[nodiscard]] bool pointer_in_buffer ( char * p ) const noexcept { return m_storage <= p and p <= m_storage + Size; }
};

template<class Type, std::size_t Size, std::size_t Align = alignof ( std::max_align_t )>
class stack_allocator {

    public:
    using value_type                             = Type;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::false_type;
    using propagate_on_container_swap            = std::false_type;
    using is_always_equal                        = std::false_type;

    static auto constexpr alignment = Align;
    static auto constexpr size      = Size;
    using storage_type              = aligned_stack_storage_<size, alignment>;

    private:
    storage_type & a_;

    public:
    stack_allocator ( stack_allocator const & ) = delete;

    stack_allocator & operator= ( stack_allocator const & ) = delete;

    stack_allocator ( storage_type & a ) noexcept : a_ ( a ) {
        static_assert ( size % alignment == 0, "size Size needs to be a multiple of alignment Align" );
    }

    template<class U>
    stack_allocator ( const stack_allocator<U, Size, alignment> & a ) noexcept : a_ ( a.a_ ) {}

    template<class _Up>
    struct rebind {
        using other = stack_allocator<_Up, Size, alignment>;
    };

    Type * allocate ( std::size_t n ) { return reinterpret_cast<Type *> ( a_.allocate ( n * sizeof ( Type ) ) ); }
    void deallocate ( Type * p, std::size_t n ) noexcept { a_.deallocate ( reinterpret_cast<char *> ( p ), n * sizeof ( Type ) ); }

    template<std::size_t A1, class U, std::size_t M, std::size_t A2>
    friend inline bool operator== ( stack_allocator<Type, Size, A1> const & x, stack_allocator<U, M, A2> const & y ) noexcept {
        return Size == M && A1 == A2 && &x.a_ == &y.a_;
    }

    template<std::size_t A1, class U, std::size_t M, std::size_t A2>
    friend inline bool operator!= ( stack_allocator<Type, Size, A1> const & x, stack_allocator<U, M, A2> const & y ) noexcept {
        return !( x == y );
    }

    template<class U, std::size_t M, std::size_t A>
    friend class stack_allocator;
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
