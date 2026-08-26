import os
import sys

_src_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src")
if _src_dir not in sys.path:
    sys.path.insert(0, _src_dir)

from bartool.bartool import main

if __name__ == "__main__":
    main()

