# Bready – Odoo Integration Discord Bot

**Bready** is a Discord bot that connects your server to [Odoo](https://odoo.com)
(versions 16–19, SaaS and self-hosted). Built and maintained by
**All Things Toasty Software Ltd**.

---

## Prerequisites

- **CMake** ≥ 3.16
- A **C++17** compiler (GCC ≥ 9 or Clang ≥ 9)
- **libcurl** development headers (`libcurl4-openssl-dev` on Debian/Ubuntu)
- An internet connection at build time (CMake fetches D++ and nlohmann/json
  via `FetchContent`)

---

## Building

```bash
cmake -B build
cmake --build build
```

The compiled binary will be at `build/bready`.

---

## Running

```bash
export DISCORD_BOT_TOKEN="your_discord_bot_token"

# Odoo shared-bot credentials (Odoo 16-19, optional – used as fallback):
export ODOO_URL="https://yourcompany.odoo.com"
export ODOO_DB="your_database"
export ODOO_USER="you@example.com"
export ODOO_API_KEY="your_api_key"

# Optional: restrict commands to users who hold a specific role:
export DISCORD_ADMIN_ROLE_ID="123456789012345678"
export DISCORD_MEMBER_ROLE_ID="987654321098765432"

./build/bready
```

---

## Repository Structure

```
bready/                         # Odoo integration Discord bot
├── .github/                    # GitHub configuration
│   ├── workflows/
│   │   └── build-bready.yml    # CI build workflow for bready 
│   ├── ISSUE_TEMPLATE/
│   │   ├── bug_report.md       # Bug report template
│   │   ├── feature_request.md  # Feature request template
│   │   └── config.yml          # Issue template chooser
│   └── PULL_REQUEST_TEMPLATE.md
├── LICENSE                     # CC BY-NC-ND 4.0 (applies to the whole repository)
├── README.md                   # This file
├── CONTRIBUTING.md             # Contribution guidelines
├── CMakeLists.txt
├── include/                    # Header files (namespace bready)
└── src/                        # Source files
```

---

## Code Style

This project follows the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html):

- 2-space indentation, no tabs.
- Maximum 80 characters per line.
- Include guards follow the pattern `INCLUDE_<FILENAME>_H_`.
- All code lives in the `bready` namespace.
- Every `.h` and `.cc` file begins with the copyright header.

---

## License

Copyright © 2026 All Things Toasty Software Ltd.

Licensed under the
[Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International](https://creativecommons.org/licenses/by-nc-nd/4.0/)
licence. See [LICENCE](LICENSE) for the full text.