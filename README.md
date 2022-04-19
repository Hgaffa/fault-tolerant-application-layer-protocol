# CS347 Coursework
To build and run:
1. Install [Docker](https://docs.docker.com/get-docker/) and [Docker-Compose](https://docs.docker.com/compose/install/).
2. Navigate to the directory in which `docker-compose.yml` is located.
3. Run `docker-compose up --build`. You may need to run this command with superuser based on how you installed docker.
4. The heartbeats will continue after transfer has completed. These should be stopped with Ctrl+C.

To run the system test:
1. Remain in the directory one level above 'tests' (i.e. where `docker-compose.yml` is located).
2. Run `./full_test_with_fail.sh`.

The system test uses an arbitrary delay to attempt to kill the server during transfer but this is not easily made consistent because of the low transfer times so this should be manually configured within the script.
