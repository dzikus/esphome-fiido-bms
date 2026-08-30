FROM mcr.microsoft.com/devcontainers/python:3.14

USER root
COPY requirements.txt /tmp/requirements.txt
RUN grep -v '^homeassistant' /tmp/requirements.txt > /tmp/tools.txt \
    && pip install --no-cache-dir -r /tmp/tools.txt \
    && pip install --no-cache-dir --no-deps "$(grep '^homeassistant' /tmp/requirements.txt)" \
    && rm /tmp/requirements.txt /tmp/tools.txt
USER vscode
