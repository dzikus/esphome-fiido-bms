FROM mcr.microsoft.com/devcontainers/python:3.14@sha256:3b3c45535947751e9dfcd513ef5bee858d221844f55e278e92d1cd8f5b1f2972

USER root
COPY requirements.txt /tmp/requirements.txt
RUN grep -v '^homeassistant' /tmp/requirements.txt > /tmp/tools.txt \
    && pip install --no-cache-dir -r /tmp/tools.txt \
    && pip install --no-cache-dir --no-deps "$(grep '^homeassistant' /tmp/requirements.txt)" \
    && rm /tmp/requirements.txt /tmp/tools.txt
USER vscode
