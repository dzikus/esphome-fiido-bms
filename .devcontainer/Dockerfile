FROM mcr.microsoft.com/devcontainers/python:3.14

USER root
RUN pip install --no-cache-dir "esphome==2026.8.1" "platformio==6.1.19" "pre-commit==4.6.2" \
    "ruff==0.16.4" "clang-tidy==22.1.8" \
    && pip install --no-cache-dir --no-deps "homeassistant==2026.8.3"
USER vscode
