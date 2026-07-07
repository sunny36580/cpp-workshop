"""
跨平台中文字体加载工具

Linux 上 pygame.font.match_font() 对中文字体名经常返回 None，
即使字体已安装。Windows 则直接使用 SysFont("microsoftyahei") 即可。
"""

import subprocess
import os
import sys

import pygame

# ---------- 平台检测 ----------
_IS_WINDOWS = sys.platform == "win32" or sys.platform == "cygwin"

# ---------- Windows：直接用 SysFont ----------
_WIN_CN_FONT_NAME = "microsoftyahei"
_WIN_CN_MONO_NAME = "microsoftyahei"


def _load_font_windows(size, bold=False, mono=False):
    """Windows 上用 SysFont 直接指定中文字体名，效果可靠"""
    name = _WIN_CN_MONO_NAME if mono else _WIN_CN_FONT_NAME
    font = pygame.font.SysFont(name, size, bold=bold)
    # 验证字体是否真的包含中文（随便渲染一个中文字看宽高是否正常）
    test_surf = font.render("中", True, (0, 0, 0))
    if test_surf.get_width() > 4:
        return font
    return pygame.font.SysFont(None, size, bold=bold)


# ---------- Linux：多级查找策略 ----------
_LINUX_CN_FONT_PATHS = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
    "/usr/share/fonts/truetype/noto/NotoSansCJKsc-Regular.ttf",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
    "/usr/share/fonts/opentype/source-han-sans-sc/SourceHanSansSC-Regular.otf",
    "/usr/share/fonts/truetype/source-han-sans-sc/SourceHanSansSC-Regular.ttf",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
]

_LINUX_CN_MONO_PATHS = [
    "/usr/share/fonts/opentype/noto/NotoSansMonoCJKsc-Regular.otf",
    "/usr/share/fonts/truetype/noto/NotoSansMonoCJK-Regular.ttf",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
]

_LINUX_FONT_CANDIDATES = [
    "notosanscjksc", "notosanscjk", "sourcehansanssc",
    "wenquanyimicrohei", "wqyzenhei",
    "noto sans cjk sc", "noto sans cjk",
    "wqy-microhei", "wqy-zenhei",
    "microsoftyahei", "simhei",
]

_LINUX_MONO_CANDIDATES = [
    "notosansmonocjksc", "notosansmonocjk", "notosanscjksc",
    "noto sans mono cjk sc",
    "wenquanyimicrohei", "wqyzenhei", "wqy-zenhei", "wqy-microhei",
]


def _linux_find_by_fc_list(mono=False):
    """使用 fc-list 查询系统已安装的中文字体文件路径"""
    try:
        result = subprocess.run(
            ["fc-list", ":lang=zh", "-f", "%{file}\n"],
            capture_output=True, text=True, timeout=3.0,
        )
        if result.returncode != 0 or not result.stdout.strip():
            return None
        paths = [p.strip() for p in result.stdout.strip().split("\n") if p.strip()]
        if not paths:
            return None
        seen = set()
        unique = []
        for p in paths:
            key = os.path.basename(p)
            if key not in seen:
                seen.add(key)
                unique.append(p)
        if mono:
            for p in unique:
                if "mono" in os.path.basename(p).lower() and os.path.exists(p):
                    return p
        for p in unique:
            if os.path.exists(p):
                return p
    except Exception:
        pass
    return None


def _linux_find_by_common_paths(mono=False):
    """按常见字体文件路径查找"""
    paths = _LINUX_CN_MONO_PATHS if mono else _LINUX_CN_FONT_PATHS
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def _linux_find_by_match_font(mono=False, bold=False):
    """按字体名使用 pygame.match_font 查找"""
    candidates = _LINUX_MONO_CANDIDATES if mono else _LINUX_FONT_CANDIDATES
    for name in candidates:
        font_path = pygame.font.match_font(name, bold=bold)
        if font_path and os.path.exists(font_path):
            return font_path
    return None


def _load_font_linux(size, bold=False, mono=False):
    """Linux 多级策略加载中文字体"""
    font_path = _linux_find_by_fc_list(mono=mono)
    if not font_path:
        font_path = _linux_find_by_common_paths(mono=mono)
    if not font_path:
        font_path = _linux_find_by_match_font(mono=mono, bold=bold)
    if font_path:
        try:
            font = pygame.font.Font(font_path, size)
            font.set_bold(bold)
            return font
        except Exception:
            pass
    print("⚠️ 未找到中文字体，请安装 fonts-noto-cjk 或 fonts-wqy-microhei")
    return pygame.font.SysFont(None, size, bold=bold)


def load_ui_font(size, bold=False, mono=False):
    """根据当前操作系统选择合适的中文字体加载方式"""
    if _IS_WINDOWS:
        return _load_font_windows(size, bold=bold, mono=mono)
    return _load_font_linux(size, bold=bold, mono=mono)
