FROM mcr.microsoft.com/devcontainers/python:3.15@sha256:6e261c4a01a94ab86485a6b9d08083c3e968ea51440c0c24b6efaa771c33312d

USER root
COPY requirements.txt /tmp/requirements.txt
RUN grep -v '^homeassistant' /tmp/requirements.txt > /tmp/tools.txt \
    && pip install --no-cache-dir -r /tmp/tools.txt \
    && pip install --no-cache-dir --no-deps "$(grep '^homeassistant' /tmp/requirements.txt)" \
    && rm /tmp/requirements.txt /tmp/tools.txt
USER vscode
RUN mkdir -p /home/vscode/.cache/esphome /home/vscode/.platformio
