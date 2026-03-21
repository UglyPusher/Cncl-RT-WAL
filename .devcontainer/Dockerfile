FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    gdb \
    git \
    clang \
    clang-tidy \
    clang-format \
    bear \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /build
