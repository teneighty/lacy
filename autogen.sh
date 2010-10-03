#!/bin/bash

if [ "$1" = "clean" ]; then
    make clean
    cat .gitignore | xargs -i rm -rf {}
else
    autoreconf --install
fi

