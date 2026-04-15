import os
import shutil
import glob

print('=' * 30, 'setup_tft_espi.py', '=' * 30)
print(f'Current working directory: {os.getcwd()}')

# 使用当前工作目录作为基准（PlatformIO 在项目根目录运行脚本）
project_dir = os.getcwd()
assets_dir = os.path.join(project_dir, 'assets')
print(f'Assets directory: {assets_dir}')

# 源文件路径
src_header = os.path.join(assets_dir, 'esp32_ST7789.h')
src_user_setup = os.path.join(assets_dir, 'User_Setup_Select.h')
src_lvconf_setup = os.path.join(assets_dir, 'lv_conf.h')

# 动态查找 TFT_eSPI 库路径
def find_tft_espi_path():
    """在 .pio/libdeps 下查找 TFT_eSPI 库的路径"""
    libdeps_base = os.path.join(project_dir, '.pio', 'libdeps')
    
    if not os.path.exists(libdeps_base):
        print(f'Error: {libdeps_base} not found. Please build the project first.')
        return None
    
    # 查找所有环境下的 TFT_eSPI 目录
    possible_paths = glob.glob(os.path.join(libdeps_base, '*', 'TFT_eSPI'))
    
    if not possible_paths:
        print(f'Error: TFT_eSPI not found in {libdeps_base}')
        print('Please build the project at least once to download the library.')
        return None
    
    # 使用第一个找到的（通常只有一个环境）
    tft_path = possible_paths[0]
    print(f'Found TFT_eSPI at: {tft_path}')
    return tft_path

# 强制复制函数
def force_copy(src, dst):
    if not os.path.exists(src):
        print(f'Warning: Source file not found: {src}')
        return
    
    if os.path.exists(dst):
        print(f'Removing existing file: {dst}')
        os.remove(dst)
    
    # 确保目标目录存在
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    
    print(f'Copying {os.path.basename(src)} -> {os.path.basename(dst)}')
    shutil.copy(src, dst)

# 复制函数（如果不存在）
def no_update_copy(src, dst):
    if not os.path.exists(src):
        print(f'Warning: Source file not found: {src}')
        return
        
    if not os.path.exists(dst):
        # 确保目标目录存在
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        print(f'Copying {os.path.basename(src)} -> {os.path.basename(dst)}')
        shutil.copy(src, dst)
    else:
        print(f'{os.path.basename(dst)} exists, skipping copy')

# 查找 TFT_eSPI 路径
tft_espi_path = find_tft_espi_path()

if tft_espi_path:
    # 目标路径
    dst_header = os.path.join(tft_espi_path, 'esp32_ST7789.h')
    dst_user_setup = os.path.join(tft_espi_path, 'User_Setup_Select.h')
    dst_lvconf_setup = os.path.join(tft_espi_path, '..', 'lv_conf.h')
    
    # 执行复制
    force_copy(src_header, dst_header)
    force_copy(src_user_setup, dst_user_setup)
    # no_update_copy(src_lvconf_setup, dst_lvconf_setup)
else:
    print('Warning: Could not find TFT_eSPI library path.')
    print('This is normal on first build - the library will be downloaded first.')
    print('The files will be copied on the next build.')

print('=' * 30, 'setup_tft_espi.py', '=' * 30)
