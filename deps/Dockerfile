# Currently this only supports x86 architecture
# Build this using:
#    sudo docker build -t gw .
# Run using:
#    sudo docker run -it --cpus=4 --memory=8g -p 9876:9876 --mount src=/Users/sbi8kc2/Desktop/data,target=/home/mambauser/Desktop/data,type=bind gw

FROM mambaorg/micromamba:latest

# Set up environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV DISPLAY=:1

# Add micromamba to PATH
ENV PATH=/opt/conda/bin:$PATH

# Install system dependencies
# If building gw froum source add:
# clang ninja-build libharfbuzz-dev libwebp-dev \
USER root
RUN apt-get update && apt-get install -y \
    tigervnc-standalone-server \
    tigervnc-common \
    novnc \
    websockify \
    openssh-client \
    xfce4 \
    xfce4-terminal \
    dbus-x11 \
    vim \
    git curl wget \
    sudo \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*


RUN adduser mambauser sudo && \
    echo "mambauser ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers


RUN micromamba install -y -n base -c conda-forge -c bioconda \
    samtools bcftools \
    && micromamba clean --all --yes
#RUN micromamba shell init --shell bash --root-prefix=~/.local/share/mamba

# Set up proper permissions for mambauser
RUN chown -R mambauser:mambauser /opt/conda && \
    chmod -R 775 /opt/conda

RUN micromamba shell init --shell bash --root-prefix=~/.local/share/mamba

# Create necessary directories with proper permissions
RUN mkdir -p /home/.ssh /home/data /home/.vnc && \
    chown -R mambauser:mambauser /home && \
    chmod 700 /home/.ssh && \
    chmod 777 /home/data

# Set up VNC password. Create .Xauthority file
RUN echo "password" | vncpasswd -f > /home/.vnc/passwd && \
    chmod 600 /home/.vnc/passwd && \
    chown -R mambauser:mambauser /home/.vnc && \
    touch /root/.Xauthority && \
    touch /home/.Xauthority && \
    chown mambauser:mambauser /home/.Xauthority

RUN bash -c 'if [ ! -f /usr/share/novnc/utils/launch.sh ]; then \
    mkdir -p /usr/share/novnc/utils && \
    ln -s /usr/bin/websockify /usr/share/novnc/utils/launch.sh; \
    fi'

# Script to deal with activation and services
RUN touch /usr/local/bin/start.sh && \
 echo "#!/bin/bash" >> /usr/local/bin/start.sh && \
 echo 'eval "$(micromamba shell hook --shell bash)"' >> /usr/local/bin/start.sh && \
 echo "micromamba activate base" >> /usr/local/bin/start.sh && \
 echo ". /opt/conda/etc/profile.d/mamba.sh" >> /usr/local/bin/start.sh && \
 echo "micromamba activate base" >> /usr/local/bin/start.sh && \
 echo "echo 'Starting VNC server using display:${DISPLAY}'" >> /usr/local/bin/start.sh && \
 echo "vncserver \$DISPLAY -geometry 1280x800 -depth 24 -SecurityTypes None" >> /usr/local/bin/start.sh && \
 echo "echo 'Starting noVNC...'" >> /usr/local/bin/start.sh && \
 echo "websockify --web=/usr/share/novnc 9876 localhost:5901 &" >> /usr/local/bin/start.sh && \
 echo "echo 'Services started. You can now connect to http://localhost:9876/vnc.html'" >> /usr/local/bin/start.sh && \
 echo "echo 'Installed tools:'" >> /usr/local/bin/start.sh && \
 echo "echo 'gw version: '\$(gw --version 2>&1)" >> /usr/local/bin/start.sh && \
 echo "echo 'samtools version: '\$(samtools --version | head -n1)" >> /usr/local/bin/start.sh && \
 echo "echo 'bcftools version: '\$(bcftools --version | head -n1)" >> /usr/local/bin/start.sh && \
 echo "tail -f /dev/null" >> /usr/local/bin/start.sh


RUN chmod +x /usr/local/bin/start.sh && \
    chown mambauser:mambauser /usr/local/bin/start.sh

WORKDIR /home
EXPOSE 9876

USER mambauser

ENTRYPOINT ["/usr/local/bin/_entrypoint.sh", "start.sh"]
