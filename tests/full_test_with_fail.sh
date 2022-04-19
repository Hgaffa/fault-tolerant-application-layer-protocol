#!/bin/bash


docker-compose build
docker-compose up -d server-1
docker-compose up -d server-2
docker-compose up client &

sleep 3.0 # has to be configured to coincide with connection start in order to insert failure at correct time

docker kill server-1
wait

echo "Detailed logs including servers:"

docker-compose logs
docker-compose down
cmp client/bigfile.jpeg server/bigfile.jpeg && echo "File received identical to original"
