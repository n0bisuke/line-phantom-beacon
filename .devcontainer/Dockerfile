# PlatformIO 電子工作(ESP32/AtomS3)用 devcontainer (GitHub Codespaces 対応)
# 共通環境 shared-dev-environments/pio の Dockerfile と同じ構成。
# 参考: docs/handson.md
FROM python:3.13-slim-bookworm

ENV DEBIAN_FRONTEND=noninteractive \
    PLATFORMIO_CORE_DIR=/root/.platformio \
    PATH="/root/.local/bin:${PATH}"

# build-essential(gcc/g++)は `pio test -e native`(ボード不要のユニットテスト)に必要。
# gh は Codespaces 内での git push / PR 作成に便利。
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      curl ca-certificates git ripgrep jq less unzip xz-utils make \
      build-essential gh && \
    rm -rf /var/lib/apt/lists/*

# PlatformIO Core はバージョン固定。esptool は緊急時の merge_bin / write-flash 用。
ARG PLATFORMIO_VERSION=6.2.0
RUN pip install --no-cache-dir "platformio==${PLATFORMIO_VERSION}" esptool

WORKDIR /workspace
CMD ["/bin/bash"]
