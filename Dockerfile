FROM leimao/boost:1.84.0

WORKDIR /app

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/boost.conf && ldconfig

COPY CMakeLists.txt .

COPY . .

RUN mkdir build && \
    cd build && \
    cmake -D BOOST_ROOT=/usr/local .. && \
    cmake --build . -j$(nproc)

RUN cp experiments/script.sh ./script.sh

CMD ["/app/script.sh"]