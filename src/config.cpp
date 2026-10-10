#include "radar/config.hpp"

#include <type_traits>

namespace radar {

// Compile-time guarantees for the value semantics required of Config: it is a
// Rule-of-Zero value type that can be default-constructed and freely copied or
// moved into the owning Application.
static_assert(std::is_default_constructible_v<Config>);
static_assert(std::is_copy_constructible_v<Config>);
static_assert(std::is_move_constructible_v<Config>);
static_assert(std::is_copy_assignable_v<Config>);

}  // namespace radar
