FROM mcr.microsoft.com/devcontainers/python:3.14@sha256:28835c112fd097cb6d2fe1e195b660d66ae051859070a9b871fa2308f35897d3

USER root
COPY requirements.txt /tmp/requirements.txt
RUN grep -v '^homeassistant' /tmp/requirements.txt > /tmp/tools.txt \
    && pip install --no-cache-dir -r /tmp/tools.txt \
    && pip install --no-cache-dir --no-deps "$(grep '^homeassistant' /tmp/requirements.txt)" \
    && rm /tmp/requirements.txt /tmp/tools.txt
USER vscode
RUN mkdir -p /home/vscode/.cache/esphome /home/vscode/.platformio
