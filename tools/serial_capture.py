#!/usr/bin/env python3
"""
serial_capture.py — Mở thẳng 1 COM port, capture log, lưu file + in log filter ra stdout.

Dùng khi cần đọc serial monitor mà không muốn tự viết Ctrl+C / timeout logic, hoặc khi
`pio device monitor` bị chiếm cổng bởi 1 process khác (VS Code Serial Monitor, PlatformIO
IDE...). Tool này dùng pyserial mở thẳng COM port, đọc đến khi user nhấn Ctrl+C (hoặc hết
--duration), ghi log kèm timestamp vào file, và in ra màn hình các dòng khớp filter (mặc
định: các LOG() tag thực tế của project — xem src/cantim_mqtt_new.cpp, src/mqtt.cpp).

Usage:
    python tools/serial_capture.py                      # auto-detect port, 115200, log ra logs/serial.log (repo root)
    python tools/serial_capture.py --port COM4
    python tools/serial_capture.py --baud 9600
    python tools/serial_capture.py --out my_test.log     # resolve theo CWD hiện tại, không phải repo root
    python tools/serial_capture.py --all                 # in TẤT CẢ dòng (không filter)
    python tools/serial_capture.py --duration 30         # tự thoát sau 30s

Cài pyserial (nếu chưa có), dùng đúng python của PlatformIO penv:
    ~/.platformio/penv/Scripts/python.exe -m pip install pyserial
"""
import argparse
import sys
import time
from datetime import datetime
from pathlib import Path

# Repo root = thư mục cha của tools/ — anchor tuyệt đối, không phụ thuộc CWD lúc gọi script.
REPO_ROOT = Path(__file__).resolve().parent.parent

DEFAULT_PORT = "auto"  # khớp upload_port/monitor_port = auto trong platformio.ini
DEFAULT_BAUD = 115200
DEFAULT_OUT = None  # sentinel "user chưa truyền --out"; resolve thật ở main() dùng REPO_ROOT
DEFAULT_OUT_REL = "logs/serial.log"  # chỉ để hiện trong help text

# Tag thực tế trong LOG(...) của project (Ethernet / MQTT / RS485 parsing / trạng thái).
FILTER_TAGS = ("ETH", "MQTT", "FULL", "MISSING", "Loi:", "khong hop le", "overflow", "Failed", "failed", "HTTP Server")


def timestamp() -> str:
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def autodetect_port() -> str:
    from serial.tools import list_ports

    ports = list(list_ports.comports())
    if not ports:
        print("[!] Không tìm thấy COM port nào đang cắm.", file=sys.stderr)
        sys.exit(2)
    if len(ports) > 1:
        print("[!] Nhiều hơn 1 COM port đang cắm, cần chỉ định --port:", file=sys.stderr)
        for p in ports:
            print(f"    {p.device}  ({p.description})", file=sys.stderr)
        sys.exit(2)
    return ports[0].device


def parse_args():
    ap = argparse.ArgumentParser(description="Capture serial monitor log to file.")
    ap.add_argument("--port", default=DEFAULT_PORT,
                     help=f"Serial port, hoặc 'auto' để tự dò khi chỉ có 1 COM port cắm (default: {DEFAULT_PORT})")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Baud rate (default: {DEFAULT_BAUD})")
    ap.add_argument("--out", default=DEFAULT_OUT,
                     help=f"Output log file (default: {DEFAULT_OUT_REL} ở repo root; nếu truyền --out thì resolve theo CWD hiện tại)")
    ap.add_argument("--all", action="store_true", help="Print ALL lines to stdout (no filter)")
    ap.add_argument("--duration", type=int, default=0, help="Auto-exit after N seconds (0 = until Ctrl+C)")
    return ap.parse_args()


def main():
    args = parse_args()
    try:
        import serial  # pyserial
    except ImportError:
        print("[!] pyserial chưa cài. Chạy:", file=sys.stderr)
        print("    ~/.platformio/penv/Scripts/python.exe -m pip install pyserial", file=sys.stderr)
        sys.exit(1)

    port = autodetect_port() if args.port == "auto" else args.port

    if args.out is None:
        out_path = (REPO_ROOT / DEFAULT_OUT_REL).resolve()
    else:
        out_path = Path(args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"[serial_capture] port={port} baud={args.baud} out={out_path}", file=sys.stderr)
    print(f"[serial_capture] filter={'ALL' if args.all else FILTER_TAGS}", file=sys.stderr)
    print(f"[serial_capture] Ctrl+C để dừng" + (f"; tự thoát sau {args.duration}s" if args.duration else ""),
          file=sys.stderr)

    try:
        ser = serial.Serial(port, args.baud, timeout=1)
    except PermissionError:
        print(f"[!] {port} đang bị chiếm bởi process khác (VS Code Serial Monitor? pio device monitor?).", file=sys.stderr)
        print("    Đóng process đó rồi chạy lại.", file=sys.stderr)
        sys.exit(2)
    except serial.SerialException as e:
        print(f"[!] Không mở được {port}: {e}", file=sys.stderr)
        sys.exit(2)

    # Reset ESP32 qua DTR/RTS toggle để bắt full boot log (optional, an toàn — auto reset
    # circuit chuẩn của board dev USB-CDC).
    try:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        ser.dtr = True
    except Exception:
        pass

    deadline = time.time() + args.duration if args.duration else None
    line_count = 0
    filter_count = 0
    out_f = open(out_path, "w", encoding="utf-8")

    try:
        while True:
            if deadline and time.time() > deadline:
                print(f"\n[serial_capture] hết {args.duration}s, tự thoát.", file=sys.stderr)
                break
            raw = ser.readline()
            if not raw:
                continue
            try:
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            except Exception:
                line = repr(raw)
            ts = timestamp()
            tagged = f"{ts} | {line}"
            out_f.write(tagged + "\n")
            out_f.flush()
            line_count += 1
            matches = args.all or any(tag in line for tag in FILTER_TAGS)
            if matches:
                filter_count += 1
                print(tagged, flush=True)
    except KeyboardInterrupt:
        print("\n[serial_capture] Ctrl+C -> dừng.", file=sys.stderr)
    finally:
        out_f.close()
        ser.close()
    print(f"[serial_capture] đã ghi {line_count} dòng vào {out_path} ({filter_count} match filter).",
          file=sys.stderr)


if __name__ == "__main__":
    main()
