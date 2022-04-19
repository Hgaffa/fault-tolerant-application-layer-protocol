# Application-layer Protocol For Highly Available File Server Downloads
This research explores the implementation of a fault tolerant application layer protocol in which transparent & unidrectional connection migration is supported within a network of support servers. Existing methods for attaining high availability for networking applications centres around transport and network layer implementation. We explore a potential application layer protocol for unidirectional downloads of static resources. 

To emulate the client-server scenario, a number of docker containers with networking capabilities have been utilised. A file download is emulated between a client machine and a server with a fault injected to mirror a server failure. By using a heartbeat mechanism with soft state synchronisations, the connection is migrated to a support server with the download resuming with full transparency. A full report detailing the research can be found in this repository. 

To build and run the demo:
1. Install [Docker](https://docs.docker.com/get-docker/) and [Docker-Compose](https://docs.docker.com/compose/install/).
2. Navigate to the directory in which `docker-compose.yml` is located.
3. Run `docker-compose up --build`. You may need to run this command with superuser based on how you installed docker.
4. The heartbeats will continue after transfer has completed. These should be stopped with Ctrl+C.

To run the system test:
1. Remain in the directory one level above 'tests' (i.e. where `docker-compose.yml` is located).
2. Run `./full_test_with_fail.sh`.

The system test uses an arbitrary delay to attempt to kill the server during transfer but this is not easily made consistent because of the low transfer times so this should be manually configured within the script.
