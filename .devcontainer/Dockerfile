FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG VERILATOR_VERSION=v4.216
ARG MILL_VERSION=0.12.17
ARG SBT_VERSION=1.10.7

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl git make g++ pkg-config python3 \
        autoconf openjdk-17-jdk gtkwave flex bison \
        gcc-riscv64-linux-gnu g++-riscv64-linux-gnu \
        libfl-dev libncurses-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl --fail --location \
        "https://github.com/sbt/sbt/releases/download/v${SBT_VERSION}/sbt-${SBT_VERSION}.tgz" \
        --output /tmp/sbt.tgz \
    && tar -xzf /tmp/sbt.tgz -C /opt \
    && ln -s /opt/sbt/bin/sbt /usr/local/bin/sbt \
    && rm /tmp/sbt.tgz

RUN curl --fail --location \
        "https://raw.githubusercontent.com/com-lihaoyi/mill/${MILL_VERSION}/mill" \
        --output /usr/local/bin/mill \
    && chmod +x /usr/local/bin/mill

RUN git clone --depth 1 --branch "${VERILATOR_VERSION}" https://github.com/verilator/verilator.git /tmp/verilator \
    && cd /tmp/verilator \
    && autoconf \
    && ./configure \
    && make -j"$(nproc)" \
    && make install \
    && rm -rf /tmp/verilator

RUN git config --global core.autocrlf input \
    && git config --global --add safe.directory '*' \
    && git config --global user.name "MoonGrt" \
    && git config --global user.email "1561145394@qq.com"

WORKDIR /workspace/OpenGPU
CMD ["bash"]
