Import("env")
import os
from datetime import datetime
from pathlib import Path

# Read SHA from environment (GitHub Actions or local fallback)
git_sha = os.getenv("GIT_SHA", "dev")
build_date = datetime.utcnow().strftime("%Y-%m-%d")

# Prefer include/ over src/ for headers
version_h = Path(env.subst("$PROJECT_INCLUDE_DIR")) / "version.h"

version_h.write_text(
    f"""#pragma once
#define FW_VERSION "{git_sha}"
#define BUILD_DATE "{build_date}"
"""
)

print(f"[version] Generated FW_VERSION={git_sha}")