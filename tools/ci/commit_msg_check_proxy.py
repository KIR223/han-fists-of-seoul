import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), ""))
from commit_msg_check import is_russian  # noqa: E402,F401
