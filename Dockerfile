FROM leimao/boost:1.84.0

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/boost.conf && ldconfig

WORKDIR /combos

COPY . /combos

RUN mkdir build

WORKDIR /combos/build

RUN cmake -D BOOST_ROOT=/usr/local ..

RUN cmake --build .