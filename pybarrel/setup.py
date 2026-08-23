import os
import sys
import shutil
from setuptools import Extension, setup

EXTERNAL_LIB_DIR = os.path.join("..", "libbarrel")
LOCAL_LIB_DIR = "libbarrel_local"

if os.path.exists(EXTERNAL_LIB_DIR):
    if os.path.exists(LOCAL_LIB_DIR):
        shutil.rmtree(LOCAL_LIB_DIR)
    shutil.copytree(EXTERNAL_LIB_DIR, LOCAL_LIB_DIR)

macros = []

if sys.platform == "win32":
    macros.append(("BRL_WIN", None))
elif sys.platform == "darwin":
    macros.append(("BRL_MACOS", None))
    macros.append(("BRL_POSIX", None))
elif sys.platform.startswith("linux"):
    macros.append(("BRL_LINUX", None))
    macros.append(("BRL_POSIX", None))
elif os.name == "posix":
    macros.append(("BRL_POSIX", None))

DEBUG_BUILD = os.environ.get("BRL_DEBUG", "0") == "1" or "--debug" in sys.argv

if DEBUG_BUILD:
    macros.append(("BRL_DEBUG", None))
else:
    macros.append(("BRL_RELEASE", None))

barrel_extension = Extension(
    name="pybarrel._abi",
    sources=[
        "csrc/abi.c",
        os.path.join(LOCAL_LIB_DIR, "src", "barrel.c"),
    ],
    include_dirs=[
        os.path.join(LOCAL_LIB_DIR, "inc"),
        os.path.join(LOCAL_LIB_DIR, "src"),
    ],
    define_macros=macros,
    extra_compile_args=["-g" if DEBUG_BUILD else "-O3"] if os.name != "nt" else ["/Zi" if DEBUG_BUILD else "/O2"],
)

setup(
    name="pybarrel",
    version="1.0.0",
    description="Python bindings for the Barrel library",
    package_dir={"": "pysrc"},
    packages=["pybarrel"],
    ext_modules=[barrel_extension],
    python_requires=">=3.8",
)
