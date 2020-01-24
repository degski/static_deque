
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

namespace sax {

namespace detail {

inline namespace static_deque {}

} // namespace detail

template<typename Type, typename SizeType, std::size_t ChunkSize = 512u / sizeof ( Type )>
class static_deque {

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
    }
};

} // namespace sax
