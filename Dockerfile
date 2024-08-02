FROM alpine
RUN apk add --no-cache bison cmake pkgconf python3 \
    openssl-dev bzip2-dev lz4-dev pcre-dev readline-dev xz-dev zlib-dev \
    build-base
WORKDIR /src/monetdb
COPY . .
RUN cmake -S . -B build && \
    cmake --build build && \
    cmake --install build --prefix /mybin
