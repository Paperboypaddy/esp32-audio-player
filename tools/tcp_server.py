import socket
import threading
import os
import time
import curses

RCV_BUF_SIZE = 8194
SND_BUF_SIZE = 8194
SERVER_PORT = 8080
MP3_FOLDER = "./mp3_files"
CLIENT_TIMEOUT = 10  # Timeout in seconds for idle clients
SCAN_INTERVAL = 10   # Seconds to scan for new files

current_file_index = 0
pause_flag = threading.Event()
skip_flag = threading.Event()
active_clients = set()
lock = threading.Lock()
songs = []

def scan_for_new_files():
    """Continuously scan for new MP3 files and update the list when detected."""
    global songs
    while True:
        time.sleep(SCAN_INTERVAL)
        with lock:
            new_count = len([f for f in os.listdir(MP3_FOLDER) if f.endswith('.mp3')])
            if new_count != len(songs):
                songs = sorted([f for f in os.listdir(MP3_FOLDER) if f.endswith('.mp3')])
                print(f"Updated song list. Total files: {len(songs)}")

def handle_client(client_socket, client_address):
    """Handle an individual client connection and stream MP3 files sequentially."""
    global current_file_index

    with lock:
        active_clients.add(client_address[0])

    try:
        client_socket.settimeout(CLIENT_TIMEOUT)
        client_socket.send(b"get msg, will download")

        while True:
            with lock:
                if current_file_index >= len(songs):
                    current_file_index = 0  # Loop back if out of range
                file_name = songs[current_file_index]
            
            file_path = os.path.join(MP3_FOLDER, file_name)

            with open(file_path, "rb") as fo:
                while True:
                    if pause_flag.is_set():
                        time.sleep(1)
                        continue

                    file_msg = fo.read(1024)
                    if not file_msg:
                        break

                    try:
                        client_socket.send(file_msg)
                    except socket.timeout:
                        return

                    if skip_flag.is_set():
                        skip_flag.clear()
                        break

            with lock:
                current_file_index = (current_file_index + 1) % len(songs)

    except (socket.error, socket.timeout):
        pass

    finally:
        with lock:
            active_clients.discard(client_address[0])
        client_socket.close()

def control_interface(stdscr):
    """Curses-based interface for controlling playback and showing active clients."""
    global current_file_index

    curses.curs_set(0)
    stdscr.nodelay(1)

    while True:
        stdscr.clear()

        stdscr.addstr(0, 0, "=== ESP32 MP3 Streaming Server ===", curses.A_BOLD)
        stdscr.addstr(2, 0, "Controls: [P] Pause/Resume | [S] Skip | [Number] Jump to Song | [Q] Quit")

        # Show available songs
        stdscr.addstr(4, 0, "Available Songs:")
        with lock:
            if songs:
                for idx, song in enumerate(songs):
                    highlight = curses.A_REVERSE if idx == current_file_index else curses.A_NORMAL
                    stdscr.addstr(5 + idx, 2, f"[{idx}] {song}", highlight)
            else:
                stdscr.addstr(5, 2, "No songs available")

        # Display active clients
        stdscr.addstr(7 + len(songs), 0, "Active Clients:")
        with lock:
            if active_clients:
                for idx, client in enumerate(active_clients):
                    stdscr.addstr(8 + len(songs) + idx, 2, f"- {client}")
            else:
                stdscr.addstr(8 + len(songs), 2, "No active clients")

        # Display current status
        pause_status = "PAUSED" if pause_flag.is_set() else "PLAYING"
        stdscr.addstr(10 + len(songs), 0, f"Currently Playing: {current_file_index}/{len(songs)} [{pause_status}]")

        stdscr.addstr(12 + len(songs), 0, "Waiting for input...")

        stdscr.refresh()
        try:
            key = stdscr.getch()
            if key == ord('p'):
                pause_flag.set() if not pause_flag.is_set() else pause_flag.clear()
            elif key == ord('s'):
                skip_flag.set()
            elif key == ord('q'):
                break
            elif 48 <= key <= 57:  # Numbers 0-9
                song_index = key - 48
                with lock:
                    if 0 <= song_index < len(songs):
                        current_file_index = song_index
                        skip_flag.set()
        except:
            pass
        time.sleep(0.5)

def start_tcp_server(ip, port):
    """Start a TCP server to allow multiple clients and serve sequential MP3 files."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind((ip, port))
    sock.listen(5)
    print(f"Server is listening on {ip}:{port}...")

    while True:
        try:
            client_socket, client_address = sock.accept()
            client_thread = threading.Thread(target=handle_client, args=(client_socket, client_address))
            client_thread.start()
        except KeyboardInterrupt:
            print("Server shutting down...")
            break

    sock.close()

def run_server_and_ui():
    """Run the server, UI, and folder scanner concurrently."""
    global songs
    songs = sorted([f for f in os.listdir(MP3_FOLDER) if f.endswith('.mp3')])  # Initial scan

    server_thread = threading.Thread(target=start_tcp_server, args=(socket.gethostbyname(socket.getfqdn(socket.gethostname())), SERVER_PORT))
    server_thread.daemon = True
    server_thread.start()

    scanner_thread = threading.Thread(target=scan_for_new_files)
    scanner_thread.daemon = True
    scanner_thread.start()

    curses.wrapper(control_interface)

if __name__ == '__main__':
    run_server_and_ui()
