# Contributing to TarvosTool

Thanks for your interest in TarvosTool. Bug reports, hardware reports, documentation fixes and code are all welcome, and you are free to fork the project (see [License](#license-and-forks)).

Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Ways to help

- **Report a bug.** Open an issue with what you did, what you expected and what happened. Include your OS, the USB-UART adapter, the module and its firmware version, and the relevant part of the Communication Log.
- **Report what works.** Results with other Tarvos-e firmware versions, adapters or boards are valuable, because the tool has only been used with a small set of hardware.
- **Improve the documentation.** Fixes to the README and this guide are always welcome.
- **Fix a bug or add a feature.** For anything bigger than a small fix, open an issue first so the approach can be agreed before you spend time on it.

## How changes get in

Nobody pushes directly to `main`, including the maintainer. Every change arrives as a pull request and is reviewed by the maintainer before it is merged.

1. Fork the repository and create a branch from `main`, for example `fix-baud-validation`.
2. Make your change and build it (see the [README](README.md#build-and-run)).
3. Commit, push to your fork and open a pull request. The pull request template asks for the information a reviewer needs.
4. Wait for CI. The macOS and Windows builds must pass.
5. The maintainer reviews the change and may ask for adjustments. When it is approved it is merged.

Review is done on a best-effort basis, so please be patient.

## Size of a pull request

**A pull request may change at most 300 lines and touch at most 10 files.** Lines are counted as added plus removed lines in source and build files (`src/`, `include/`, `CMakeLists.txt`, `.github/`). Documentation (`*.md`), screenshots and the credits file do not count.

The limit exists because small changes get reviewed carefully and quickly. 300 lines is roughly a ninth of the current code base and about what a person can review properly in half an hour. If your change is larger:

- split it into steps that each build and make sense on their own;
- keep refactoring, renames and formatting changes in separate pull requests from behaviour changes;
- for a feature that cannot be split, open an issue first. The maintainer can agree to an exception in advance.

Pull requests over the limit without an agreed exception will be asked to split.

## Code guidelines

- **Language and style.** C++17 with wxWidgets. Follow the style of the surrounding code: four-space indentation, `PascalCase` for functions and types, `m_` prefix for members, `kName` for constants, `///` comments on declarations in headers.
- **Keep the layers apart.** `TarvosProtocol` contains frame building, parsing, decoding and the settings table, and has no GUI and no I/O. `SerialPort` is the only place that touches the operating system's serial API, and platform code stays inside its `#ifdef _WIN32` split. `MainFrame` and `ChatFrame` are the user interface.
- **Follow the manual.** Protocol behaviour follows the Tarvos-e user manual, revision 2.18. When you add or change protocol behaviour, mention the manual section in a comment and in the pull request.
- **No new dependencies** without discussing them in an issue first. The project currently needs only wxWidgets.
- **Both platforms.** A change must not break the macOS or the Windows build.

## Testing

The repository has no automated test suite yet, so tell us how you tested in the pull request: operating system, adapter, module and firmware, and what you did. For changes to `TarvosProtocol`, check frames against the examples in the manual; the file compiles on its own, so a small test program is enough.

## Commit messages

Use a short imperative subject of at most 72 characters (`Fix channel range check`), then a blank line and a few lines explaining why the change is needed. One logical change per commit.

## Credit

- **Your authorship is kept.** Pull requests are merged in a way that preserves you as the author of your commits, so you show up in the repository history and in GitHub's contributors list.
- **You are listed in [CONTRIBUTORS.md](CONTRIBUTORS.md)** when your first pull request is merged. The pull request template asks which name you want listed. The maintainer adds the entry, so you do not need to edit that file.
- If someone else helped with a change, add a `Co-authored-by: Name <email>` line to the commit message.
- You keep the copyright of your contribution. The original copyright notice stays in [LICENSE](LICENSE).

## License and forks

TarvosTool is released under the [MIT License](LICENSE). By submitting a contribution you agree that it is licensed under the same terms.

You do not need permission to fork the project, change it or use it in your own work. The MIT License asks one thing in return: keep the copyright notice and license text, which credits the original author.
