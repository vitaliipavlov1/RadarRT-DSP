# Contributing

Thanks for your interest in RadarRT-DSP! Please read [ARCHITECTURE.md](ARCHITECTURE.md)
first — the decisions D1–D8 are binding, and changes should follow them.

## Local workflow

CI runs the same commands, so a clean local run matches CI.

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug

# sanitizers
cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan
cmake --preset tsan       && cmake --build --preset tsan       && ctest --preset tsan
```

## Quality gates

| Gate | Command |
| --- | --- |
| Formatting | `clang-format-18 --dry-run --Werror $(find include src app tests -name '*.hpp' -o -name '*.cpp')` |
| Static analysis | `cmake --preset debug && clang-tidy-18 -p build/debug $(find src app tests -name '*.cpp')` |
| Tests | `ctest --preset <debug\|release\|asan-ubsan\|tsan>` |

Warnings are errors (`RADAR_WARNINGS_AS_ERRORS=ON`) and clang-tidy findings fail CI.

## Pull requests

* Keep changes focused; add or update unit tests for behavioural changes.
* Time-dependent logic must use the injected `Clock` — no real sleeps in tests.
* Describe *why*, not just *what*, in the PR description.

## License

By contributing, you agree that your contributions are licensed under the
[Apache License 2.0](LICENSE).
