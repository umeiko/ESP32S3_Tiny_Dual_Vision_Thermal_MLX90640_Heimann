#!/usr/bin/env python3
"""
ESP32 双光复合流上位机
协议: COMPBEGIN + T_max(4B float) + T_min(4B float) + 768*float(32x24) + JPEG + COMPEND

依赖安装:
    pip install pyserial pillow

使用示例:
    python composite_client.py
"""

import io
import serial
import struct
import threading
import time
import tkinter as tk
from tkinter import ttk

import serial.tools.list_ports
from PIL import Image, ImageDraw, ImageFont, ImageTk


class CompositeViewer:
    # 热成像参数 (MLX90640)
    THERMAL_WIDTH = 32
    THERMAL_HEIGHT = 24
    THERMAL_PIXELS = THERMAL_WIDTH * THERMAL_HEIGHT  # 768

    # 协议常量
    COMP_MARKER_BEGIN = b'COMPBEGIN'
    COMP_MARKER_END = b'COMPEND'
    HEADER_SIZE = 9   # COMPBEGIN
    FOOTER_SIZE = 7   # COMPEND
    META_SIZE = 8     # T_max_fp + T_min_fp
    THERMAL_DATA_SIZE = THERMAL_PIXELS * 4  # 768 * 4 bytes float
    FIXED_PREFIX_SIZE = HEADER_SIZE + META_SIZE + THERMAL_DATA_SIZE  # 3089

    # 喂狗间隔 (ms)，下位机超时 1000ms
    WATCHDOG_INTERVAL = 500

    def __init__(self, master):
        self.master = master
        self.master.title("双光复合流查看器")
        self.master.geometry("1000x620")

        # 流控制
        self.is_streaming = False
        self.stream_running = False
        self.stream_thread = None
        self.serial_connection = None
        self.watchdog_timer = None

        # 接收缓冲区
        self.receive_buffer = b''
        self.buffer_lock = threading.Lock()

        # 当前帧数据
        self.thermal_frame = None      # 32x24 原始温度矩阵
        self.display_thermal_frame = None  # 变换后温度矩阵（供鼠标悬浮直接查表）
        self.current_tmax = None
        self.current_tmin = None
        self.visible_image = None      # PIL Image 可见光

        # Canvas 图像 ID
        self.visible_canvas_id = None
        self.thermal_canvas_id = None

        # 鼠标追踪（用于悬停不动时随帧刷新温度）
        self.last_mouse_x = -1
        self.last_mouse_y = -1
        self.mouse_in_thermal = False

        # 可见光变换变量（默认270°、垂直翻转）
        self.visible_rotate_var = tk.IntVar(value=270)
        self.visible_hflip_var = tk.BooleanVar(value=False)
        self.visible_vflip_var = tk.BooleanVar(value=False)

        # 热成像变换变量（默认0°、垂直翻转、双线性插值）
        self.thermal_rotate_var = tk.IntVar(value=0)
        self.thermal_hflip_var = tk.BooleanVar(value=False)
        self.thermal_vflip_var = tk.BooleanVar(value=True)
        self.thermal_interpolated = tk.BooleanVar(value=True)

        # 颜色映射
        self.color_map = self._create_thermal_colormap()

        self._setup_ui()
        self.refresh_ports()

    # ------------------------------------------------------------------
    # 颜色映射
    # ------------------------------------------------------------------
    def _create_thermal_colormap(self):
        """创建热力图颜色映射表 (0-255)"""
        colormap = []
        for i in range(256):
            t = i / 255.0
            if t < 0.125:
                r = int(128 * (t / 0.125)); g = 0; b = int(128 + 127 * (t / 0.125))
            elif t < 0.25:
                r = int(128 - 128 * ((t - 0.125) / 0.125)); g = 0; b = 255
            elif t < 0.375:
                r = 0; g = int(255 * ((t - 0.25) / 0.125)); b = 255
            elif t < 0.5:
                r = 0; g = 255; b = int(255 - 255 * ((t - 0.375) / 0.125))
            elif t < 0.625:
                r = int(255 * ((t - 0.5) / 0.125)); g = 255; b = 0
            elif t < 0.75:
                r = 255; g = int(255 - 128 * ((t - 0.625) / 0.125)); b = 0
            elif t < 0.875:
                r = 255; g = int(127 - 127 * ((t - 0.75) / 0.125)); b = 0
            else:
                r = 255; g = int(128 * ((t - 0.875) / 0.125)); b = int(128 * ((t - 0.875) / 0.125))
            colormap.append((r, g, b))
        return colormap

    def _temp_to_color(self, temp, t_min, t_max):
        if t_max <= t_min:
            t_max = t_min + 1
        normalized = int(255 * (temp - t_min) / (t_max - t_min))
        normalized = max(0, min(255, normalized))
        return self.color_map[normalized]

    # ------------------------------------------------------------------
    # 图像/矩阵变换
    # ------------------------------------------------------------------
    @staticmethod
    def _make_transform(rotate_var, hflip_var, vflip_var):
        return {
            'rotate': rotate_var.get(),
            'hflip': hflip_var.get(),
            'vflip': vflip_var.get(),
        }

    @staticmethod
    def _transform_matrix(matrix, transform):
        """对二维温度矩阵做与 PIL 相同的 rotate/flip 变换"""
        rot = transform['rotate']
        if rot == 90:
            h = len(matrix)
            w = len(matrix[0])
            matrix = [[matrix[h - 1 - y][x] for y in range(h)] for x in range(w)]
        elif rot == 180:
            matrix = [list(reversed(row)) for row in reversed(matrix)]
        elif rot == 270:
            h = len(matrix)
            w = len(matrix[0])
            matrix = [[matrix[y][w - 1 - x] for y in range(h)] for x in range(w)]
        if transform['hflip']:
            matrix = [list(reversed(row)) for row in matrix]
        if transform['vflip']:
            matrix = list(reversed(matrix))
        return matrix

    def _create_thermal_image(self, temp_matrix, t_min, t_max, width, height, transform):
        """创建热成像图像：先做矩阵变换，再逐像素着色，最后 resize"""
        matrix = self._transform_matrix(temp_matrix, transform)
        rows = len(matrix)
        cols = len(matrix[0]) if rows else 0

        img = Image.new('RGB', (cols, rows))
        pixels = img.load()
        for y in range(rows):
            for x in range(cols):
                pixels[x, y] = self._temp_to_color(matrix[y][x], t_min, t_max)

        resample = Image.BILINEAR if self.thermal_interpolated.get() else Image.NEAREST
        img = img.resize((width, height), resample)
        return img, matrix

    def _apply_transform_img(self, img, transform):
        """对 PIL Image 做旋转变换（仅用于可见光）"""
        rot = transform['rotate']
        if rot == 90:
            img = img.transpose(Image.ROTATE_270)
        elif rot == 180:
            img = img.transpose(Image.ROTATE_180)
        elif rot == 270:
            img = img.transpose(Image.ROTATE_90)
        if transform['hflip']:
            img = img.transpose(Image.FLIP_LEFT_RIGHT)
        if transform['vflip']:
            img = img.transpose(Image.FLIP_TOP_BOTTOM)
        return img

    # ------------------------------------------------------------------
    # UI
    # ------------------------------------------------------------------
    def _setup_ui(self):
        main_frame = tk.Frame(self.master)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # ---------- 顶部控制栏 ----------
        ctrl_frame = tk.Frame(main_frame)
        ctrl_frame.pack(fill=tk.X, pady=5)

        tk.Label(ctrl_frame, text="串口:").pack(side=tk.LEFT, padx=5)
        self.port_combobox = ttk.Combobox(ctrl_frame, state="readonly", width=22)
        self.port_combobox.pack(side=tk.LEFT, padx=5)

        self.refresh_button = tk.Button(
            ctrl_frame, text="刷新", command=self.refresh_ports, width=8
        )
        self.refresh_button.pack(side=tk.LEFT, padx=5)

        self.connect_button = tk.Button(
            ctrl_frame, text="连接", command=self.toggle_connect,
            width=10, bg='lightblue'
        )
        self.connect_button.pack(side=tk.LEFT, padx=5)

        self.start_stream_button = tk.Button(
            ctrl_frame, text="▶ 开始串流", command=self.start_stream,
            bg='lightgreen', state='disabled'
        )
        self.start_stream_button.pack(side=tk.LEFT, padx=5)

        self.stop_stream_button = tk.Button(
            ctrl_frame, text="⏹ 停止", command=self.stop_stream,
            bg='lightcoral', state='disabled'
        )
        self.stop_stream_button.pack(side=tk.LEFT, padx=5)

        self.status_label = tk.Label(ctrl_frame, text="状态: 未连接", fg='red')
        self.status_label.pack(side=tk.RIGHT, padx=10)

        # ---------- 双画面横排 ----------
        img_frame = tk.Frame(main_frame)
        img_frame.pack(fill=tk.BOTH, expand=True, pady=5)

        # 可见光
        visible_frame = tk.LabelFrame(img_frame, text="可见光")
        visible_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)
        self.visible_canvas = tk.Canvas(visible_frame, bg='black', highlightthickness=0)
        self.visible_canvas.pack(fill=tk.BOTH, expand=True)

        vis_btn_frame = tk.Frame(visible_frame)
        vis_btn_frame.pack(fill=tk.X, pady=2)
        tk.Label(vis_btn_frame, text="旋转:").pack(side=tk.LEFT, padx=2)
        for angle in (0, 90, 180, 270):
            tk.Radiobutton(vis_btn_frame, text=f"{angle}°", variable=self.visible_rotate_var,
                           value=angle, command=self._update_display).pack(side=tk.LEFT, padx=1)
        tk.Checkbutton(vis_btn_frame, text="水平翻转", variable=self.visible_hflip_var,
                       command=self._update_display).pack(side=tk.LEFT, padx=8)
        tk.Checkbutton(vis_btn_frame, text="垂直翻转", variable=self.visible_vflip_var,
                       command=self._update_display).pack(side=tk.LEFT, padx=2)

        # 热成像
        thermal_frame = tk.LabelFrame(img_frame, text="热成像")
        thermal_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)
        self.thermal_canvas = tk.Canvas(thermal_frame, bg='black', highlightthickness=0)
        self.thermal_canvas.pack(fill=tk.BOTH, expand=True)

        thm_btn_frame = tk.Frame(thermal_frame)
        thm_btn_frame.pack(fill=tk.X, pady=2)
        tk.Label(thm_btn_frame, text="旋转:").pack(side=tk.LEFT, padx=2)
        for angle in (0, 90, 180, 270):
            tk.Radiobutton(thm_btn_frame, text=f"{angle}°", variable=self.thermal_rotate_var,
                           value=angle, command=self._update_display).pack(side=tk.LEFT, padx=1)
        tk.Checkbutton(thm_btn_frame, text="水平翻转", variable=self.thermal_hflip_var,
                       command=self._update_display).pack(side=tk.LEFT, padx=8)
        tk.Checkbutton(thm_btn_frame, text="垂直翻转", variable=self.thermal_vflip_var,
                       command=self._update_display).pack(side=tk.LEFT, padx=2)
        tk.Checkbutton(thm_btn_frame, text="双线性插值", variable=self.thermal_interpolated,
                       command=self._update_display).pack(side=tk.LEFT, padx=8)

        # 绑定热成像鼠标事件
        self.thermal_canvas.bind('<Motion>', self._on_thermal_mouse_move)
        self.thermal_canvas.bind('<Leave>', self._on_thermal_mouse_leave)

        # ---------- 温度信息 ----------
        info_frame = tk.Frame(main_frame)
        info_frame.pack(fill=tk.X, pady=5)

        self.tmax_label = tk.Label(
            info_frame, text="最高温: --°C", fg='red', font=('Arial', 12, 'bold')
        )
        self.tmax_label.pack(side=tk.LEFT, padx=10)

        self.tmin_label = tk.Label(
            info_frame, text="最低温: --°C", fg='blue', font=('Arial', 12, 'bold')
        )
        self.tmin_label.pack(side=tk.LEFT, padx=10)

        self.tcenter_label = tk.Label(
            info_frame, text="中心温: --°C", font=('Arial', 10)
        )
        self.tcenter_label.pack(side=tk.LEFT, padx=10)

        self.tmouse_label = tk.Label(
            info_frame, text="悬浮: --°C", font=('Arial', 10), fg='green'
        )
        self.tmouse_label.pack(side=tk.LEFT, padx=10)

        # 初始占位
        self._draw_placeholder(self.visible_canvas, "可见光\n无信号")
        self._draw_placeholder(self.thermal_canvas, "热成像\n无信号")

    def _draw_placeholder(self, canvas, text):
        """在 Canvas 上绘制居中文本，并清理图像 ID"""
        canvas.delete('all')
        if canvas is self.visible_canvas:
            self.visible_canvas_id = None
        elif canvas is self.thermal_canvas:
            self.thermal_canvas_id = None
            self.mouse_in_thermal = False
        w = canvas.winfo_width() or 400
        h = canvas.winfo_height() or 300
        canvas.create_text(w // 2, h // 2, text=text, fill='white', font=('Arial', 16))

    # ------------------------------------------------------------------
    # 鼠标悬浮温度（直接从变换后矩阵查表）
    # ------------------------------------------------------------------
    def _on_thermal_mouse_move(self, event):
        self.mouse_in_thermal = True
        self.last_mouse_x = event.x
        self.last_mouse_y = event.y
        self._update_mouse_temp()

    def _on_thermal_mouse_leave(self, event):
        self.mouse_in_thermal = False
        self.tmouse_label.config(text="悬浮: --°C")

    def _update_mouse_temp(self):
        """根据最后记录的鼠标位置刷新悬浮温度（被 _update_display 每帧调用）"""
        if not self.mouse_in_thermal or self.display_thermal_frame is None:
            return
        try:
            cw = self.thermal_canvas.winfo_width()
            ch = self.thermal_canvas.winfo_height()
            if cw <= 1 or ch <= 1:
                return

            rows = len(self.display_thermal_frame)
            cols = len(self.display_thermal_frame[0]) if rows else 0
            if rows == 0 or cols == 0:
                return

            cx = int(self.last_mouse_x * cols / cw)
            cy = int(self.last_mouse_y * rows / ch)

            if 0 <= cx < cols and 0 <= cy < rows:
                temp = self.display_thermal_frame[cy][cx]
                self.tmouse_label.config(text=f"悬浮: ({cx},{cy}) {temp:.1f}°C")
            else:
                self.tmouse_label.config(text="悬浮: --°C")
        except Exception as e:
            print(f"鼠标温度计算错误: {e}")

    # ------------------------------------------------------------------
    # 串口
    # ------------------------------------------------------------------
    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [p.device for p in ports]
        self.port_combobox['values'] = port_list
        if port_list:
            self.port_combobox.set(port_list[0])

    def toggle_connect(self):
        if self.serial_connection is None:
            port = self.port_combobox.get()
            if not port:
                self.status_label.config(text="请先选择串口", fg='red')
                return
            try:
                self.serial_connection = serial.Serial(port, 115200, timeout=0.1)
                self.connect_button.config(text="断开", bg='lightcoral')
                self.status_label.config(text=f"已连接 {port}", fg='green')
                self.start_stream_button.config(state='normal')
            except Exception as e:
                self.status_label.config(text=f"连接失败: {e}", fg='red')
                self.serial_connection = None
        else:
            self.stop_stream()
            try:
                self.serial_connection.close()
            except Exception:
                pass
            self.serial_connection = None
            self.connect_button.config(text="连接", bg='lightblue')
            self.status_label.config(text="状态: 未连接", fg='red')
            self.start_stream_button.config(state='disabled')
            self.stop_stream_button.config(state='disabled')
            self._draw_placeholder(self.visible_canvas, "可见光\n无信号")
            self._draw_placeholder(self.thermal_canvas, "热成像\n无信号")

    # ------------------------------------------------------------------
    # 流控制
    # ------------------------------------------------------------------
    def start_stream(self):
        if not self.serial_connection or not self.serial_connection.is_open:
            return
        self.is_streaming = True
        self.stream_running = True

        with self.buffer_lock:
            self.receive_buffer = b''
        try:
            self.serial_connection.reset_input_buffer()
        except Exception:
            pass

        self.serial_connection.write(b'composite stream\n')

        self.stream_thread = threading.Thread(target=self._stream_receive_thread)
        self.stream_thread.daemon = True
        self.stream_thread.start()

        self._start_watchdog()
        self.status_label.config(text="状态: 串流中...", fg='blue')
        self.start_stream_button.config(state='disabled')
        self.stop_stream_button.config(state='normal')
        self._schedule_ui_update()

    def stop_stream(self):
        self.is_streaming = False
        self.stream_running = False
        self._stop_watchdog()
        if self.stream_thread and self.stream_thread.is_alive():
            self.stream_thread.join(timeout=1.0)
        if self.serial_connection:
            try:
                self.serial_connection.write(b'\n')
            except Exception:
                pass
        self.status_label.config(text="状态: 已停止", fg='green')
        self.start_stream_button.config(state='normal')
        self.stop_stream_button.config(state='disabled')

    def _start_watchdog(self):
        if self.watchdog_timer:
            self.master.after_cancel(self.watchdog_timer)
            self.watchdog_timer = None
        self._feed_watchdog()

    def _stop_watchdog(self):
        if self.watchdog_timer:
            self.master.after_cancel(self.watchdog_timer)
            self.watchdog_timer = None

    def _feed_watchdog(self):
        if self.is_streaming and self.serial_connection:
            try:
                if self.serial_connection.is_open:
                    self.serial_connection.write(b'composite stream\n')
            except Exception as e:
                print(f"喂狗失败: {e}")
                self._handle_disconnect()
                return
            self.watchdog_timer = self.master.after(self.WATCHDOG_INTERVAL, self._feed_watchdog)

    def _handle_disconnect(self):
        print("串口已断开")
        self.is_streaming = False
        self.stream_running = False
        self._stop_watchdog()
        if self.stream_thread and self.stream_thread.is_alive():
            try:
                self.stream_thread.join(timeout=0.5)
            except Exception:
                pass
        if self.serial_connection:
            try:
                self.serial_connection.close()
            except Exception:
                pass
            self.serial_connection = None
        self.connect_button.config(text="连接", bg='lightblue')
        self.status_label.config(text="状态: 串口已断开", fg='red')
        self.start_stream_button.config(state='disabled')
        self.stop_stream_button.config(state='disabled')

    # ------------------------------------------------------------------
    # 接收线程
    # ------------------------------------------------------------------
    def _stream_receive_thread(self):
        while self.stream_running and self.serial_connection:
            try:
                if not self.serial_connection.is_open:
                    break
                available = self.serial_connection.in_waiting
                if available > 0:
                    data = self.serial_connection.read(available)
                    with self.buffer_lock:
                        self.receive_buffer += data
                        while True:
                            if not self._try_process_frame():
                                break
                else:
                    time.sleep(0.001)
            except serial.SerialException as e:
                print(f"串口异常: {e}")
                self._handle_disconnect()
                break
            except Exception as e:
                print(f"接收错误: {e}")
                time.sleep(0.01)

    def _try_process_frame(self):
        if len(self.receive_buffer) < self.HEADER_SIZE:
            return False

        begin_pos = self.receive_buffer.find(self.COMP_MARKER_BEGIN)
        if begin_pos == -1:
            self.receive_buffer = self.receive_buffer[-(self.HEADER_SIZE + 10):]
            return False

        if begin_pos > 0:
            self.receive_buffer = self.receive_buffer[begin_pos:]

        min_len = self.FIXED_PREFIX_SIZE + self.FOOTER_SIZE
        if len(self.receive_buffer) < min_len:
            return False

        end_pos = self.receive_buffer.find(self.COMP_MARKER_END, self.FIXED_PREFIX_SIZE)
        if end_pos == -1:
            if len(self.receive_buffer) > 100000:
                self.receive_buffer = self.receive_buffer[-(self.HEADER_SIZE + 10):]
            return False

        frame_data = self.receive_buffer[self.HEADER_SIZE:end_pos]
        self.receive_buffer = self.receive_buffer[end_pos + self.FOOTER_SIZE:]

        self._parse_composite_frame(frame_data)
        return True

    def _parse_composite_frame(self, data):
        try:
            if len(data) < self.META_SIZE + self.THERMAL_DATA_SIZE:
                print(f"帧数据过短: {len(data)} bytes")
                return

            t_max = struct.unpack('<f', data[0:4])[0]
            t_min = struct.unpack('<f', data[4:8])[0]

            thermal_bytes = data[8:8 + self.THERMAL_DATA_SIZE]
            pixels = struct.unpack('<' + 'f' * self.THERMAL_PIXELS, thermal_bytes)

            temp_matrix = []
            for y in range(self.THERMAL_HEIGHT):
                row = []
                for x in range(self.THERMAL_WIDTH):
                    row.append(pixels[y * self.THERMAL_WIDTH + x])
                temp_matrix.append(row)

            self.thermal_frame = temp_matrix
            self.current_tmax = t_max
            self.current_tmin = t_min

            jpeg_data = data[8 + self.THERMAL_DATA_SIZE:]
            if len(jpeg_data) > 0:
                img = Image.open(io.BytesIO(jpeg_data))
                self.visible_image = img.convert('RGB')

        except Exception as e:
            print(f"解析复合帧错误: {e}")

    # ------------------------------------------------------------------
    # 显示更新
    # ------------------------------------------------------------------
    def _schedule_ui_update(self):
        if self.is_streaming:
            self._update_display()
            self.master.after(50, self._schedule_ui_update)

    def _update_display(self):
        # ---- 可见光 ----
        if self.visible_image:
            try:
                w = self.visible_canvas.winfo_width()
                h = self.visible_canvas.winfo_height()
                if w > 1 and h > 1:
                    img = self.visible_image.copy()
                    vis_transform = self._make_transform(
                        self.visible_rotate_var, self.visible_hflip_var, self.visible_vflip_var
                    )
                    img = self._apply_transform_img(img, vis_transform)
                    img = img.resize((w, h), Image.LANCZOS)
                    self.visible_photo = ImageTk.PhotoImage(img)
                    if self.visible_canvas_id:
                        self.visible_canvas.itemconfig(self.visible_canvas_id, image=self.visible_photo)
                    else:
                        self.visible_canvas_id = self.visible_canvas.create_image(
                            0, 0, anchor='nw', image=self.visible_photo
                        )
            except Exception as e:
                print(f"更新可见光错误: {e}")

        # ---- 热成像 ----
        if self.thermal_frame is not None and self.current_tmax is not None:
            try:
                w = self.thermal_canvas.winfo_width()
                h = self.thermal_canvas.winfo_height()
                if w > 1 and h > 1:
                    t_min = self.current_tmin
                    t_max = self.current_tmax
                    if t_max - t_min < 1.0:
                        center = (t_max + t_min) / 2
                        t_min = center - 0.5
                        t_max = center + 0.5

                    thm_transform = self._make_transform(
                        self.thermal_rotate_var, self.thermal_hflip_var, self.thermal_vflip_var
                    )
                    img, display_matrix = self._create_thermal_image(
                        self.thermal_frame, t_min, t_max, w, h, thm_transform
                    )
                    self.display_thermal_frame = display_matrix
                    self.thermal_photo = ImageTk.PhotoImage(img)
                    if self.thermal_canvas_id:
                        self.thermal_canvas.itemconfig(self.thermal_canvas_id, image=self.thermal_photo)
                    else:
                        self.thermal_canvas_id = self.thermal_canvas.create_image(
                            0, 0, anchor='nw', image=self.thermal_photo
                        )

                    self.tmax_label.config(text=f"最高温: {t_max:.1f}°C")
                    self.tmin_label.config(text=f"最低温: {t_min:.1f}°C")
                    cy = len(display_matrix) // 2
                    cx = len(display_matrix[0]) // 2 if display_matrix else 0
                    self.tcenter_label.config(text=f"中心温: {display_matrix[cy][cx]:.1f}°C")
            except Exception as e:
                print(f"更新热成像错误: {e}")

        # 鼠标悬停不动时，随帧刷新悬浮温度
        self._update_mouse_temp()

    # ------------------------------------------------------------------
    # 退出
    # ------------------------------------------------------------------
    def on_closing(self):
        self.stop_stream()
        if self.serial_connection:
            try:
                self.serial_connection.close()
            except Exception:
                pass
        self.master.destroy()


def main():
    root = tk.Tk()
    app = CompositeViewer(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()


if __name__ == "__main__":
    main()
