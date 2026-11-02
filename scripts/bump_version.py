#!/usr/bin/env python3
import sys
import re

if len(sys.argv) < 2:
    print("Usage: python bump_version.py <version>  (e.g., 1.1.0)")
    sys.exit(1)

new_version = sys.argv[1].strip().lstrip('v')
match = re.match(r"^(\d+)\.(\d+)\.(\d+)(.*)$", new_version)
if not match:
    print(f"Error: '{new_version}' is not a valid Semantic Version (expected X.Y.Z)")
    sys.exit(1)

major, minor, patch, extra = match.groups()

# 1. Update VERSION file
with open("VERSION", "w", encoding="utf-8") as f:
    f.write(new_version + "\n")

# 2. Update include/version.h
version_h_content = f"""// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef AKS_VERSION_H
#define AKS_VERSION_H

#define AKS_VERSION_MAJOR {major}
#define AKS_VERSION_MINOR {minor}
#define AKS_VERSION_PATCH {patch}
#define AKS_VERSION_STRING "{new_version}"
#define AKS_VERSION_WIDE L"{new_version}"

#endif // AKS_VERSION_H
"""

with open("include/version.h", "w", encoding="utf-8") as f:
    f.write(version_h_content)

print(f"AKS version successfully updated to v{new_version}")
print("  -> Updated VERSION")
print("  -> Updated include/version.h")

