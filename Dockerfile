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

ENV MONETDB_PORT=50000
ENV CREATEDB=1
ENV DBNAME="my-first-db"

COPY --from=builder /usr/local /usr/local

STOPSIGNAL SIGINT
EXPOSE ${MONETDB_PORT}

RUN apk add --no-cache libbz2 lz4-libs xz-libs && \
    [ -d /root/farm ] || monetdbd create /root/farm && \
    monetdbd set port="${MONETDB_PORT}" /root/farm && \
    if [ ${CREATEDB} -eq 1 ]; then \
      monetdbd start /root/farm && \
      cd /root/farm && \
      monetdb create "${DBNAME}" && \
      monetdbd stop /root/farm ; \
    else \
       true ; \
    fi && \
    monetdbd get all /root/farm

ENTRYPOINT ["/usr/local/bin/monetdbd", "start", "/root/farm"]
