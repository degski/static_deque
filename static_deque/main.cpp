
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

template<std::size_t N, std::size_t alignment = alignof ( std::max_align_t )>
struct chunk {
    using chunk_sptr = std::unique_ptr<chunk>;
    static_assert ( N > 16, "chunk: N is too small" );
    alignas ( alignment ) char m_buf[ N - 2 * sizeof ( char * ) ];
    char * m_ptr                  = nullptr;
    std::unique_ptr<chunk> m_next = nullptr;
    int c                         = counter++;

    constexpr chunk ( ) noexcept { std::cout << "c'tor " << c << " called" << nl; };

    ~chunk ( ) noexcept { std::cout << "d'tor " << c << " called" << nl; }
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

    using chunk      = chunk<ChunkSize, alignof ( value_type )>;
    using chunk_ptr  = chunk *;
    using chunk_sptr = std::unique_ptr<chunk>;

    static constexpr size_type chunck_size = ChunkSize;

    std::unique_ptr<chunk> m_data;
    chunk_ptr m_last_data = nullptr;

    void grow ( ) noexcept {
        if ( m_last_data ) {
            m_last_data->m_next = std::make_unique<chunk> ( );
            m_last_data         = m_last_data->m_next.get ( );
        }
        else {
            m_data      = std::make_unique<chunk> ( );
            m_last_data = m_data.get ( );
        }
    }

    pointer m_front = nullptr, m_back = nullptr;
};

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
