# Dockerfile to provide an Ubuntu environment for development and debugging
# issues in Github workflows.

FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update \
    && apt upgrade -y \
    && apt install -y \
        wget \
        curl \
        software-properties-common \
        gnupg2 \
        git \
        python3 \
        python3-dev \
        cmake \
    && wget -qO - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - \
    && add-apt-repository "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-21 main" \
    && add-apt-repository ppa:ubuntu-toolchain-r/test -y \
    && apt update \
    && apt install -y \
        clang-21 \
        llvm-21 \
        llvm-21-dev \
    # Clean up the package cache
    && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/llvm-cov-21 /usr/bin/llvm-cov \
    && ln -s /usr/bin/llvm-profdata-21 /usr/bin/llvm-profdata

CMD ["/bin/bash"]
