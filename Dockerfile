FROM leimao/boost:1.84.0

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/boost.conf && ldconfig

WORKDIR /app

COPY . /app

RUN mkdir build

WORKDIR /app/build

RUN cmake -D BOOST_ROOT=/usr/local ..

RUN cmake --build . -j4

CMD ["/app/experiments/script.sh"]