import os
import sys
from setuptools import Extension, setup

BARRELPY_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(BARRELPY_DIR, ".."))

macros = []
if sys.platform.startswith("win32"):
    macros.append(("BRL_WIN", "1"))
elif sys.platform.startswith("darwin"):
    macros.append(("BRL_MACOS", "1"))
    macros.append(("BRL_POSIX", "1"))
elif sys.platform.startswith("linux"):
    macros.append(("BRL_LINUX", "1"))
    macros.append(("BRL_POSIX", "1"))

is_debug = os.environ.get("DEBUG", "0") in ("1", "true", "True")
macros.append(("BRL_DEBUG", "1") if is_debug else ("BRL_RELEASE", "1"))

INC_DIR = os.path.join(PROJECT_ROOT, "libbarrel", "inc")
LIB_DIR = os.path.join(PROJECT_ROOT, "build", "libbarrel")

extra_link_args = []
if sys.platform.startswith("darwin"):
    extra_link_args = [f"-Wl,-rpath,{LIB_DIR}"]

barrel_c_ext = Extension(
    "_barrel",
    sources=["src/abi.c"],
    include_dirs=[INC_DIR],
    libraries=["barrel"], 
    library_dirs=[LIB_DIR],
    runtime_library_dirs=[LIB_DIR] if not sys.platform.startswith("win32") else [],
    extra_link_args=extra_link_args,
    define_macros=macros,
)

setup(
    name="barrel",
    version="1.0.0",
    py_modules=["barrel"],
    ext_modules=[barrel_c_ext],
)
