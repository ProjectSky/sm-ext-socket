# SourceMod Socket Extension

## What is this?
This is a [SourceMod](http://www.sourcemod.net/) extension that provides TCP/UDP/UDS socket communication capabilities.<br>
A complete rewrite of the [original socket extension](https://github.com/JoinedSenses/sm-ext-socket) with modern C++17 and libuv.

## Features
* Built on [libuv](https://github.com/libuv/libuv) for high-performance async I/O
* TCP/UDP/UDS client and server support
* IPv4/IPv6 support
* Configurable socket options (timeout, buffer size, keep-alive, etc.)
* Connect timeout for TCP connections
* Support x64
* Lightweight (~400KB)

## How to build this?
``` sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install clang g++-multilib
git clone --recursive https://github.com/ProjectSky/sm-ext-socket.git
cd sm-ext-socket
mkdir build && cd build
python ../configure.py --enable-optimize --sm-path=YOUR_SOURCEMOD_PATH --targets=x86,x64
ambuild
```

## Native
* [socket.inc](https://github.com/ProjectSky/sm-ext-socket/blob/main/scripting/include/socket.inc)

## Binary files
* [GitHub Releases](https://github.com/ProjectSky/sm-ext-socket/releases)

## NOTES
* Server will not process data during hibernation. Set `sv_hibernate_when_empty 0` to disable hibernation

## Example
* [Example Scripts](https://github.com/ProjectSky/sm-ext-socket/tree/main/scripting)

## TODO
* [ ] SSL/TLS support (low priority - complex to implement, use dedicated HTTP libraries for HTTPS)