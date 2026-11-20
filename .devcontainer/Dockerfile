FROM mcr.microsoft.com/devcontainers/python:3.15@sha256:0ae003ea761a00b8f8809cfdd08a04edfb1c7f17e76c413b27882ce5939a65f4

USER root
COPY requirements.txt /tmp/requirements.txt
RUN grep -v '^homeassistant' /tmp/requirements.txt > /tmp/tools.txt \
    && pip install --no-cache-dir -r /tmp/tools.txt \
    && pip install --no-cache-dir --no-deps "$(grep '^homeassistant' /tmp/requirements.txt)" \
    && rm /tmp/requirements.txt /tmp/tools.txt
USER vscode
RUN mkdir -p /home/vscode/.cache/esphome /home/vscode/.platformio
