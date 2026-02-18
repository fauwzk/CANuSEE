# extra_scripts/set_version.py

Import("env")
import os

git_sha = os.getenv("GIT_SHA", "dev")
print(f"Injecting FW_VERSION={git_sha}")
print("Current BUILD_FLAGS:", env.get("BUILD_FLAGS", []))
env.Append(BUILD_FLAGS=[f'-DFW_VERSION="{git_sha}"'])
