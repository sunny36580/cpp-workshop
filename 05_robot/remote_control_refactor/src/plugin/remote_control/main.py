"""
remote_control 包入口

用法：
    python -m remote_control.main
    python remote_control/main.py
"""

import sys
import os

# 确保包在路径中
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from remote_control.robot_remote import RobotRemote


def main():
    print("========== 三代人形机器人远程控制系统 ==========")
    app = RobotRemote()
    app.run()


if __name__ == "__main__":
    main()
