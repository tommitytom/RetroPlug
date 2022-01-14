#!/bin/bash
#echo "The script you are running has basename `basename "$0"`, dirname `dirname "$0"`"
#echo "The present working directory is `pwd`"

CONFIG="$1"
PORT="$2"

if [ -z "$CONFIG" ]; then
    CONFIG="debug"
fi

if [ -z "$PORT" ]; then
    PORT="8080"
fi

docker run --rm -it -p ${PORT}:80 -v $(pwd)/build/gmake2/${CONFIG}/:/usr/share/nginx/html -v $(pwd)/src/:/usr/share/nginx/html/src -v ~/nginx_default.conf:/etc/nginx/conf.d/default.conf nginx:mainline-alpine
