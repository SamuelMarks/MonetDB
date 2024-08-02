FROM alpine AS builder
RUN apk add --no-cache bison cmake pkgconf python3 \
    openssl-dev bzip2-dev lz4-dev pcre-dev readline-dev xz-dev zlib-dev \
    build-base
WORKDIR /src/monetdb
COPY . .
RUN cmake -S . -B build && \
    cmake --build build && \
    cmake --install build --prefix /usr/local

FROM alpine AS runner
COPY --from=builder /usr/local /usr/local
RUN apk add --no-cache bzip2-dev lz4-libs openssl-dev xz-dev zlib-dev

ENTRYPOINT ["/usr/local/bin/monetdb", "--version"]
