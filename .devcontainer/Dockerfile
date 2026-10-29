FROM mcr.microsoft.com/devcontainers/python:3.15@sha256:eb39f07323784af4c7558396808ceb01dacbe6dd7c63614f34d0235d0344a0ff

USER root
COPY requirements.txt /tmp/requirements.txt
RUN grep -v '^homeassistant' /tmp/requirements.txt > /tmp/tools.txt \
    && pip install --no-cache-dir -r /tmp/tools.txt \
    && pip install --no-cache-dir --no-deps "$(grep '^homeassistant' /tmp/requirements.txt)" \
    && rm /tmp/requirements.txt /tmp/tools.txt
USER vscode
RUN mkdir -p /home/vscode/.cache/esphome /home/vscode/.platformio
