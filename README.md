# Internet Radio Multicasting (Multimedia over IP)

This project demonstrates **Internet Radio Multicasting**, a system designed to stream multimedia content (audio, video, etc.) over IP networks. Using multicast technology, this system can efficiently broadcast multimedia data to multiple clients simultaneously, reducing network congestion and enhancing user experience for large-scale broadcasting systems.

## How to run (complete)

**Roles:** TCP catalog server (`server`), two UDP multicast senders (`station1`, `station2`), GTK client (`client`). The client can spawn `receiver`, which listens on UDP **5433** and uses **ffplay** for playback.

**Prerequisites:** `gcc`, **GTK+3** (for `client` and `receiver`), **ffmpeg** (`ffplay` on PATH). **Media:** place `vid1.mp4` … `vid8.mp4` in `sender/` (names expected by `station1.c` / `station2.c`), or generate tiny test files:

```bash
cd sender
ffmpeg -y -f lavfi -i testsrc=duration=2:size=320x240:rate=10 -pix_fmt yuv420p -c:v libx264 -t 2 _stub.mp4 -loglevel error
for f in vid1 vid3 vid4 vid5 vid6 vid7 vid8; do cp _stub.mp4 "${f}.mp4"; done
```

**Build** (from repository root):

```bash
cd sender
gcc -Wall -o server server.c
gcc -Wall -o station1 station1.c
gcc -Wall -o station2 station2.c
# optional alternate lab binary:
gcc -Wall -o sender sender.c

cd ../receiver
gcc $(pkg-config --cflags gtk+-3.0) -o client client.c $(pkg-config --libs gtk+-3.0)
gcc $(pkg-config --cflags gtk+-3.0) -o receiver receiver.c $(pkg-config --libs gtk+-3.0) -lpthread
```

**Install GTK+3 (if needed):** macOS: `brew install gtk+3`. Debian/Ubuntu: `sudo apt install libgtk-3-dev build-essential`.

**Run** (use four terminals; replace `127.0.0.1` with the catalog host if it is not local). Catalog TCP port is **15432** (avoids conflict with PostgreSQL on **5432**).

```bash
# Terminal 1 — catalog server
cd sender && ./server

# Terminal 2 — station 1 (multicast 239.192.4.1)
cd sender && ./station1 239.192.4.1

# Terminal 3 — station 2 (multicast 239.192.4.2)
cd sender && ./station2 239.192.4.2

# Terminal 4 — client GUI (then click a station button)
cd receiver && ./client 127.0.0.1
```

Picking a station in the GUI may recompile and launch `./receiver` with the matching multicast address; **ffplay** starts after enough packets are received. On Linux, the receiver may use `sudo` unless you adjust `SO_BINDTODEVICE` / interface settings in `receiver.c`.

**Stop senders/server:** press `Ctrl+C` in each terminal, or `pkill -f './server'` / `pkill -f station1` / `pkill -f station2` from the `sender` directory context as appropriate.

---

## Table of Contents

- [How to run (complete)](#how-to-run-complete)
- [Project Overview](#project-overview)
- [Features](#features)
- [Technologies Used](#technologies-used)

## Project Overview

The **Internet Radio Multicasting** project is designed to demonstrate how multimedia data can be transmitted over the Internet using multicast protocols. The project allows for the efficient delivery of multimedia content to multiple clients simultaneously, minimizing bandwidth usage compared to traditional unicast streaming.

### Key Highlights:
- **Multicasting Protocol**: Leverages the power of multicast transmission to stream audio/video data to multiple users without overloading the network.
- **Streaming Capability**: Capable of streaming audio content (Internet Radio) to clients using IP networks.
- **IP Networking**: Focuses on IP-based multimedia data transmission and includes considerations for packetizing, addressing, and stream management.
- **Scalability**: Designed to work efficiently even with a large number of clients.

## Features

- **Multicast Streaming**: Supports multicasting of multimedia content.
- **Audio Streaming**: Stream internet radio/audio to multiple users simultaneously.
- **Client Management**: Handles multiple client connections to receive multicast streams.
- **Error Handling**: Basic mechanisms to handle stream interruptions and retries.

## Technologies Used

- **Programming Language**: C (based on project implementation)
- **Networking Protocols**: UDP, Multicast
- **Audio Codec**: MP3, AAC, WAV (depending on the stream)
- **Libraries**: 
  - `socket` (for network communication)
  - `pyaudio` (for audio playback)
  - `ffmpeg` (for stream encoding and decoding)
