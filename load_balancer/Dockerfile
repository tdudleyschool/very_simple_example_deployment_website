# Dockerfile for load balancer service
FROM ubuntu:22.04

# Install required packages
RUN apt-get update && \
    apt-get install -y g++ wget && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy main.cpp
COPY main.cpp .

# Download required headers
RUN wget https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
RUN wget https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp

# Compile the load balancer
RUN g++ main.cpp -o load_balancer -std=c++17 -pthread

# Run load balancer
CMD ["./load_balancer"]

#!!! Expose port (Render will route traffic)
EXPOSE 8080