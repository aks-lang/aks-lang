# Hosted Playground Deployment Guide

This document describes how to deploy the AKS Interactive Playground to `play.aks-lang.org`.

## Overview

The AKS Interactive Playground provides:
- Monaco / CodeMirror browser-based code editing
- Zero-install code execution backend
- REST endpoint `/run` with CORS support for third-party embeds
- `/health` endpoint for monitoring and uptime checks
- Built-in sandboxed evaluation

## Quick Start (Docker)

Build and run the container locally:

```bash
docker build -t aks-playground -f deploy/Dockerfile .
docker run -d -p 8000:8000 --name aks-play aks-playground
```

Verify the service is running:

```bash
curl http://localhost:8000/health
# {"status":"ok","service":"aks-playground","version":"1.2.3"}
```

Execute code via curl:

```bash
curl -X POST http://localhost:8000/run \
  -H "Content-Type: text/plain" \
  -d 'println("Hello from play.aks-lang.org!");'
```

## Production Deployment with Nginx & SSL

1. Deploy the Docker container on the host:
   ```bash
   docker run -d --restart unless-stopped -p 127.0.0.1:8000:8000 --name aks-play aks-playground
   ```

2. Copy the Nginx configuration:
   ```bash
   cp deploy/playground.conf /etc/nginx/sites-available/play.aks-lang.org
   ln -s /etc/nginx/sites-available/play.aks-lang.org /etc/nginx/sites-enabled/
   ```

3. Obtain Let's Encrypt certificates:
   ```bash
   certbot --nginx -d play.aks-lang.org
   ```

4. Reload Nginx:
   ```bash
   nginx -t && systemctl reload nginx
   ```

