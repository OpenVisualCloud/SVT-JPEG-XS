# Contributing to SVT-JPEG-XS

Thank you for your interest in contributing. This document describes how to report problems, propose changes and
what is expected from a pull request.

## Reporting bugs and requesting features

- Report bugs and request features through [GitHub issues](https://github.com/OpenVisualCloud/SVT-JPEG-XS/issues).
- For a bug, include the library version or commit, OS and compiler, the exact command line, and, if possible, a
  small input file that reproduces the problem.
- Do not report security vulnerabilities in public issues. Follow the [security policy](security.md) instead.

## Contribution process

1. Fork the repository and create a branch from `main`.
2. Make your change, following the coding style and test policy below.
3. Build and run the unit tests locally.
4. Open a pull request against `main` and describe what the change does and why.
5. CI must pass, and at least one code owner (see [CODEOWNERS](.github/CODEOWNERS)) must approve the pull request
   before it is merged.

Keep pull requests focused: one logical change per pull request is easier to review and to revert.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/):

```text
<type>(<optional scope>): <short summary>

<optional body explaining why the change is needed>
```

Common types are `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `build`, `ci`, `style` and `chore`. Typical
scopes are `encoder`, `decoder`, `EncApp`, `DecApp`, `ffmpeg-plugin`, `gstreamer-plugin` and `mtl-plugin`.

## Coding style

- C and C++ code is formatted with `clang-format` using the repository [.clang-format](.clang-format). Run
  [format-coding.sh](format-coding.sh) before committing.
- Shell scripts, Markdown, JSON and Python files are checked with super-linter in CI
  ([linter.yaml](.github/workflows/linter.yaml)).
- GCC and Clang builds use `-Wall -Wextra -Werror`; new code must compile without warnings on all supported
  compilers.

## Platform support

x86-64 on Linux and Windows is supported and tested in CI. The AArch64 (NEON) port is community-maintained and is
not built or tested in CI. Changes to the AArch64 code must not break the x86-64 builds and tests, and the pull
request should state how the change was built and tested on AArch64.

## Building

See the [README](README.md) for requirements and build options. In short:

```bash
cd Build/linux
./build.sh test
```

On Windows:

```bat
cd Build\windows
build.bat 2022 release test
```

## Test policy

- New functionality must come with automated tests that exercise it, normally unit tests in
  [tests/UnitTests](tests/UnitTests).
- Bug fixes should include a test that fails without the fix, where practical.
- Changes to the public API in [Source/API](Source/API) must include tests in the API validation tests.
- All existing tests must pass. CI runs the unit tests on every pull request on Linux and Windows, including a run
  under valgrind.

Run the unit tests locally before opening a pull request:

```bash
Bin/Release/SvtJpegxsUnitTests
```

or in parallel:

```bash
tests/scripts/parrallelUT.sh Bin/Release/SvtJpegxsUnitTests $(nproc)
```

Conformance, encoder, sanitizer and fuzz tests are described in [documentation/tests/Readme.md](documentation/tests/Readme.md).
Changes to memory handling, bitstream parsing or threading should also be checked with the sanitizers:

```bash
tests/scripts/sanitize.sh <address|thread|undefined|integer|memory>
```

## Documentation

Update the documentation when a change affects user-visible behavior: the [README](README.md), application help
text, and the design documents in [documentation](documentation).

## License

By contributing, you agree that your contributions are licensed under the project license,
[BSD-2-Clause-Patent](LICENSE.md).
