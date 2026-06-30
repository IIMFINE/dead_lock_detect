"""为兼容历史调用方式（`deadlock_analyze trace ...`）保留的 console-script 入口。"""

from dl_analyzer.cli import main

if __name__ == "__main__":
    import sys
    sys.exit(main())
