# Contributing to Bready

Thank you for your interest in contributing to **Bready**!  
This repository contains the Discord bot **bready** (Odoo integration)
maintained by **All Things Toasty Software Ltd** and its Discord community.

Please read these guidelines before opening a pull request.

---

## Table of Contents

1. [Code of Conduct](#code-of-conduct)
2. [Getting Started](#getting-started)
3. [How to Contribute](#how-to-contribute)
4. [Code Style](#code-style)
5. [Commit Messages](#commit-messages)
6. [Pull Request Process](#pull-request-process)
7. [Reporting Bugs](#reporting-bugs)
8. [License](#license)

---

## Code of Conduct

Be respectful, inclusive, and constructive.  Harassment of any kind will not
be tolerated.

---

## Getting Started

1. Fork the repository and clone your fork:
   ```bash
   git clone https://github.com/<your-username>/bready.git
   cd bready
   ```
2. Install the prerequisites listed in the README.
3. Build the bot:
   ```bash
   cmake -B build -S bready -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j$(nproc)
   ```
4. Verify the build produces a working binary:
    - `build/bready`

5. Set the required environment variables and run the bot:

   ```bash
   export DISCORD_BOT_TOKEN="your_token"
   export ODOO_URL="https://yourcompany.odoo.com"
   export ODOO_DB="your_database"
   export ODOO_USER="you@example.com"
   export ODOO_API_KEY="your_api_key"
   ./build/bready
   ```

6. Make your changes on a feature branch:

   ```bash
   git checkout -b feature/my-improvement
   ```

---

## How to Contribute

- **Bug fixes** – open an issue first to confirm the bug, then submit a PR.
- **New features** – open an issue to discuss the design before coding.
- **Documentation** – PRs that improve docs, comments, or this file are
  always welcome.
- **Odoo integration** – features targeting the `odoo_*` commands
  should be tested against a real or a mock Odoo 17/18/19 instance.

---

## Code Style

All C++ code must follow the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html):

| Aspect | Convention |
|--------|------------|
| Source file extension | `.cc` |
| Header file extension | `.h` |
| Include guards | `INCLUDE_<FILENAME>_H_` |
| Namespace | `namespace bready { … }` |
| Types & functions | `PascalCase` |
| Variables | `snake_case` |
| Constants | `kCamelCase` |
| Line length | ≤ 80 characters |
| Indentation | 2 spaces (no tabs) |
| Copyright header | Present in every source file |

Run `clang-format` (Google style) before submitting:
```bash
clang-format -i --style=Google src/*.cc include/*.h
```

---

## Commit Messages

Use the imperative mood and keep the subject line ≤ 72 characters:

```
Add /odoo_notes command for Knowledge articles

Implement GetKnowledgeArticles() in OdooClient and wire it to a new
/odoo_notes slash command that lists the first 25 articles.
```

---

## Pull Request Process

1. Branch from `main` with a descriptive name: `feat/odoo-crm-filter`.
2. Keep each PR focused on a single change.
3. Ensure the project builds without errors or new warnings:
   ```bash
   cmake --build build -j$(nproc) 2>&1 | grep -i "error:"
   ```
4. Update the `README.md` if you add new commands or environment
   variables.
5. Request a review from a maintainer.
6. PRs that do not build will not be merged.

---

## Reporting Bugs

Please open a GitHub Issue using the **Bug report** template and include:

- A clear description of the problem.
- Steps to reproduce.
- Expected vs. actual behaviour.
- OS version.
- Relevant log output (redact tokens/keys).

---

## License

By contributing you agree that your contributions will be licensed under the
[CC BY-NC-ND 4.0](LICENSE) licence that covers this project.