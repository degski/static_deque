
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
using unique_raw_ptr = std::variant<std::unique_ptr<T>, T *>;

// To use with lambda type matching for std::variant.

template<typename... Ts>
struct overloaded : Ts... {
    using Ts::operator( )...;
};
template<typename... Ts>
overloaded ( Ts... ) -> overloaded<Ts...>;

template<typename Type>
struct wobble_ptr {
    public:
    using value_type    = Type;
    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    explicit wobble_ptr ( ) noexcept {}
    wobble_ptr ( pointer p_, bool is_unique_ = true ) noexcept : m_raw ( is_unique_ ? p_ : p_ | one_mask ) {}

    wobble_ptr ( wobble_ptr const & ) = delete;
    wobble_ptr ( wobble_ptr && rhs_ ) noexcept : m_raw ( std::exchange ( rhs_.m_raw, nullptr ) ) {}

    ~wobble_ptr ( ) noexcept { destruct ( ); }

    wobble_ptr & operator= ( wobble_ptr const & ) noexcept = delete;
    wobble_ptr & operator                                  = ( wobble_ptr && rhs_ ) noexcept {
        destruct ( );
        m_raw = std::exchange ( rhs_.m_raw, nullptr );
    }

    [[nodiscard]] const_pointer get ( ) const noexcept {
        return reinterpret_cast<const_pointer> ( reinterpret_cast<std::uintptr_t> ( m_raw ) & ptr_mask );
    }
    [[nodiscard]] pointer get ( ) noexcept { return const_cast<pointer> ( std::as_const ( *this ).get ( ) ); }

    [[nodiscard]] const_pointer operator-> ( ) const noexcept { return get ( ); }
    [[nodiscard]] pointer operator-> ( ) noexcept { return const_cast<pointer> ( std::as_const ( *this ).operator-> ( ) ); }

    void make_weak ( ) noexcept { m_raw |= one_mask; }
    void make_unique ( ) noexcept { m_raw &= ptr_mask; }

    [[nodiscard]] bool is_weak ( ) const noexcept { return reinterpret_cast<std::uintptr_t> ( m_raw ) & one_mask; }
    [[nodiscard]] bool is_unique ( ) const noexcept { return not is_weak ( ); }

    private:
    static constexpr std::uintptr_t ptr_mask = 0x00FF'FFFF'FFFF'FFF0;
    static constexpr std::uintptr_t one_mask = 0x0000'0000'0000'0001;

    pointer m_raw = nullptr;

    void destruct ( ) noexcept {
        if ( is_unique ( ) )
            delete m_raw;
    }
};
template<std::size_t N, std::align_val_t Align = static_cast<std::align_val_t> ( alignof ( std::max_align_t ) )>
struct aligned_stack_storage {

    using chk_unique_ptr     = std::unique_ptr<aligned_stack_storage>;
    using chk_unique_raw_ptr = unique_raw_ptr<aligned_stack_storage>;

    explicit constexpr aligned_stack_storage ( ) noexcept : m_next ( std::in_place_type_t<aligned_stack_storage *> ( ), this ) {
        std::cout << "c'tor " << c << " called" << nl;
    };
    aligned_stack_storage ( aligned_stack_storage const & )     = delete;
    aligned_stack_storage ( aligned_stack_storage && ) noexcept = delete;

    explicit constexpr aligned_stack_storage ( chk_unique_raw_ptr && p_ ) noexcept : m_next ( std::move ( p_ ) ) {
        std::cout << "c'tor (m_next moved in) " << c << " called" << nl;
    };
    explicit constexpr aligned_stack_storage ( chk_unique_ptr && p_ ) noexcept : m_next ( std::move ( p_ ) ) {
        std::cout << "c'tor (m_next moved in) " << c << " called" << nl;
    };

    ~aligned_stack_storage ( ) noexcept { std::cout << "d'tor " << c << " called" << nl; }

    aligned_stack_storage & operator= ( aligned_stack_storage const & ) = delete;
    aligned_stack_storage & operator= ( aligned_stack_storage && ) = delete;

    static constexpr std::size_t capacity ( ) noexcept { return char_size; };

    static constexpr std::size_t char_size = N - 2 * sizeof ( char * );

    alignas ( static_cast<std::size_t> ( Align ) ) char m_storage[ char_size ];
    unique_raw_ptr<aligned_stack_storage> m_next;
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
    using chk_raw_ptr           = aligned_stack_storage *;
    using chk_unique_ptr        = std::unique_ptr<aligned_stack_storage>;
    using chk_unique_raw_ptr    = unique_raw_ptr<aligned_stack_storage>; // smarter.

    static constexpr size_type chunck_size = static_cast<size_type> ( aligned_stack_storage::capacity ( ) / sizeof ( value_type ) );

    chk_unique_raw_ptr m_last_data;

    [[nodiscard]] static constexpr chk_unique_raw_ptr chk_allocate ( ) noexcept {
        return std::make_unique<aligned_stack_storage> ( );
    }

    [[nodiscard]] static constexpr chk_unique_raw_ptr chk_allocate ( chk_unique_raw_ptr && p_ ) noexcept {
        return std::make_unique<aligned_stack_storage> ( std::move ( p_ ) );
    }

    template<typename T = void>
    [[nodiscard]] constexpr chk_raw_ptr chk_raw ( chk_unique_raw_ptr const & sp_ ) const noexcept {
        if constexpr ( std::is_same<chk_unique_ptr, T>::value ) {
            return std::get<T> ( sp_ ).get ( );
        }
        else if constexpr ( std::is_same<chk_raw_ptr, T>::value ) {
            return std::get<T> ( sp_ );
        }
        else {
            chk_raw_ptr v = nullptr;
            std::visit ( overloaded{ [ &v ] ( chk_unique_ptr const & arg ) { v = arg.get ( ); },
                                     [ &v ] ( chk_raw_ptr const & arg ) { v = arg; } },
                         sp_ );
            assert ( v );
            return v;
        }
    }

    [[nodiscard]] constexpr chk_raw_ptr chk_raw ( chk_unique_ptr const & sp_ ) const noexcept { return sp_.get ( ); }

    [[nodiscard]] constexpr bool is_chk_unique_ptr ( chk_unique_raw_ptr const & sp_ ) const noexcept {
        return std::holds_alternative<chk_unique_ptr> ( sp_ );
    }

    [[nodiscard]] constexpr bool is_chk_raw_ptr ( chk_unique_raw_ptr const & sp_ ) const noexcept {
        return std::holds_alternative<chk_raw_ptr> ( sp_ );
    }

    // Swap the containing type, between 2 chk_unique_raw_ptr's, pointing at the same value.
    void swap_types ( chk_unique_raw_ptr & p0_, chk_unique_raw_ptr & p1_ ) const noexcept {
        // So never an object goes out of scope and never an object is held twice.
        assert ( chk_raw ( p0_ ) == chk_raw ( p1_ ) );
        bool const i0 = is_chk_raw_ptr ( p0_ );
        if ( i0 != is_chk_raw_ptr ( p1_ ) ) {
            if ( i0 ) { // rp, up.
                p1_ = std::get<chk_unique_ptr> ( p0_ = std::move ( std::get<chk_unique_ptr> ( p1_ ) ) ).get ( );
            }
            else // up, rp.
                p0_ = std::get<chk_unique_ptr> ( p1_ = std::move ( std::get<chk_unique_ptr> ( p0_ ) ) ).get ( );
        }
        else {
            // Nothing to be done.
        }
    }

    [[nodiscard]] chk_unique_raw_ptr & chk_leaf ( ) const noexcept {
        chk_raw_ptr n = chk_raw<chk_unique_ptr> ( m_last_data ); // !!!!
        while ( is_chk_unique_ptr ( n->m_next ) )
            n = chk_raw<chk_unique_ptr> ( n->m_next );
        return n->m_next;
    }

    void grow ( ) noexcept {
        if ( chk_raw ( m_last_data ) ) {

            chk_unique_raw_ptr p = std::move ( std::get<chk_unique_ptr> ( m_last_data )->m_next ); // Where was it pointing to.

            std::get<chk_unique_ptr> ( m_last_data )->m_next =
                chk_allocate ( std::move ( p ) ); // Constuct new aligned_stack_storage, and move into m_next.
            swap_types ( chk_leaf ( ), m_last_data );
            // Move last forward.
        }
        else {
            m_last_data = chk_allocate ( );
        }
    }

    pointer m_front = nullptr, m_back = nullptr;
};

int main565765 ( ) {

    std::exception_ptr eptr;

    try {
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
