#include "radar/log_record.hpp"
#include "radar/processed_signal.hpp"
#include "radar/radar_signal.hpp"

#include <type_traits>

namespace radar {

// RadarSignal and ProcessedSignal are trivially-copyable value types, so passing
// them across the pipeline performs no heap allocation on the realtime path.
static_assert(std::is_trivially_copyable_v<RadarSignal>);
static_assert(std::is_trivially_copyable_v<ProcessedSignal>);

// LogRecord is move-only with a non-throwing move, so it can travel through
// ThreadSafeQueue<LogRecord> without being copied.
static_assert(!std::is_copy_constructible_v<LogRecord>);
static_assert(!std::is_copy_assignable_v<LogRecord>);
static_assert(std::is_move_constructible_v<LogRecord>);
static_assert(std::is_move_assignable_v<LogRecord>);
static_assert(std::is_nothrow_move_constructible_v<LogRecord>);

}  // namespace radar
