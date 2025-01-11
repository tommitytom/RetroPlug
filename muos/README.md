# MustardOS development environment

## To build the docker image

```
docker build --platform linux/amd64 -t muos-developer:latest .
```

## To use the docker image to do development

```
# Assume that you are in MustardOS/frontend

docker run --platform linux/amd64 -v .:/workspace --rm -it muos-developer:latest /bin/bash
```


