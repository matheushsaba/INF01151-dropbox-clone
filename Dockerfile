# Stage 1: Builder
# We use a specific version of Ubuntu to ensure a consistent build environment.
# Ubuntu 22.04 comes with GCC 11, which satisfies the C++17 requirement.
FROM ubuntu:22.04 AS builder

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies: make and g++
RUN apt-get update && apt-get install -y build-essential g++

# Set the working directory inside the container
WORKDIR /app

# Copy all the project files into the container
COPY . .

# Clean any pre-compiled binaries from the host and then compile the application.
# This ensures the executable is built using the container's toolchain.
RUN make clean && make

# Stage 2: Runtime
# Use the same base image for runtime to ensure library compatibility.
FROM ubuntu:22.04

WORKDIR /app
# Copy only the compiled binaries from the builder stage to the final image.
COPY --from=builder /app/bin/ ./bin/

# Copy the entrypoint script, make it executable, and set it as the entrypoint.
COPY entrypoint.sh .
RUN chmod +x entrypoint.sh
ENTRYPOINT ["./entrypoint.sh"]