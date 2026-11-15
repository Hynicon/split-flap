# Build from the repository root, so the app's files (firmware/master/data) can be copied in:
#   docker compose -f server/docker-compose.yml up -d --build
FROM python:3.12-slim

WORKDIR /srv
COPY server/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY server/app.py server/oauth.py ./
COPY firmware/master/data ./static

# TRUST_PROXY: the container is reached only through nginx-proxy (no published port), so the
# proxy's own entry in X-Forwarded-For is used to count wrong passwords per visitor.
ENV STATIC_DIR=/srv/static DATA_DIR=/data TRUST_PROXY=1
VOLUME /data
EXPOSE 8000

# One worker: the queue for the display lives in this process's memory.
# --no-proxy-headers: the app reads the forwarding headers itself and never trusts the client's part.
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1", "--no-proxy-headers"]
