# extra_scripts/set_version.py
Import("env")
import os

git_sha = os.getenv("GIT_SHA", "dev")
print(f"Injecting FW_VERSION={git_sha}")

# Append to CPPDEFINES so it's visible in Arduino code
env.Append(
    CPPDEFINES=[("FW_VERSION", f'"{git_sha}"')]
)

# Optional: debug output
print("Current CPPDEFINES:", env.get("CPPDEFINES", []))
