FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       make \
       libsdl2-dev \
       libsdl2-image-dev \
       xvfb \
       x11vnc \
       openbox \
       novnc \
       websockify \
       x11-utils \
       xdotool \
       imagemagick \
       ca-certificates \
       curl \
       python3 \
    && rm -rf /var/lib/apt/lists/*

# Web 展示终端真截图验收可选；保持镜像精简，默认不装 chromium。
# 需要"网页终端截图"硬验收时构建：--build-arg WITH_CHROMIUM=1
ARG WITH_CHROMIUM=0
RUN if [ "$WITH_CHROMIUM" = "1" ]; then \
      apt-get update && apt-get install -y --no-install-recommends \
      chromium && rm -rf /var/lib/apt/lists/*; \
    fi

# ImageMagick 策略：允许 import 截图与 PS/EPS 无关操作
RUN sed -i 's/<policy domain="coder" rights="none" pattern="XPS"\/>/<!-- xps -->/' /etc/ImageMagick-6/policy.xml 2>/dev/null || true

WORKDIR /app
COPY . /app
RUN chmod +x /app/docker/entrypoint.sh /app/tools/*.py && make

EXPOSE 6080 8080
CMD ["/app/docker/entrypoint.sh"]
