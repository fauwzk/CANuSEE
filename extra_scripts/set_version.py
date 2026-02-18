Import("env")
import os
from pathlib import Path

git_sha = os.getenv("GIT_SHA", "dev")

version_h = Path(env.subst("$PROJECT_SRC_DIR")) / "version.h"

version_h.write_text(
    f'#pragma once\n#define FW_VERSION "{git_sha}"\n'
)

print(f"Generated FW_VERSION={git_sha}")