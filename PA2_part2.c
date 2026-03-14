/* 
Please specify the group members here

# Student #1: Micah

# Commented out printf's are for testing

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

int maxClients = 20;
int windowSize = 5;
char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;



typedef struct {
    int epoll_fd;
    int socket_fd;
    long long total_rtt;
    long total_messages;
    float request_rate;
} client_thread_data_t;

typedef struct
{
    int seqNum;
    int ackNum;
    int isAck;
    char message[MESSAGE_SIZE];
} packet;

typedef struct 
    {
        struct sockaddr_in theAddress;
        int nextSeq;
    } aClient;

int packetSize = sizeof(packet);

void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    packet thePacket = {.isAck = 0};
    strcpy(thePacket.message, "ABCDEFGHIJKMLNO");
    packet acket;
    struct timeval start[num_requests];
    struct timeval end;

    data->request_rate = 0;
    data->total_rtt = 0;
    data->total_messages = 0;

    // Register the socket in the epoll instance
    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event);

    int base = 0;
    int nextSeq = 0;
    
    
    while (1) {
        // Send a message
        
        while (nextSeq < base + windowSize)
        {
            gettimeofday(&start[nextSeq], NULL);

            thePacket.seqNum = nextSeq;
            sendto(data->socket_fd, &thePacket, sizeof(thePacket), 0, (struct sockaddr*)NULL, sizeof(struct sockaddr_in));
            //printf("Sent Out %d!\n", nextSeq);
            nextSeq++;
        }
        

        
        

        // Wait for the response
        int n_events = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 500);
        int acketOn = 0;
        for (int i = 0; i < n_events; i++) {
            if (events[i].data.fd == data->socket_fd) {
                recvfrom(data->socket_fd, &acket, sizeof(acket), 0, (struct sockaddr*)NULL, NULL);
                //printf("Recived ACK! %d\n", acket.ackNum);
                if (acket.isAck && acket.ackNum >= base)
                {
                    
                    base = acket.ackNum+1;
                    acketOn = 1;
                    gettimeofday(&end, NULL);
                    // Calculate RTT
                    long long rtt = (end.tv_sec - start[acket.ackNum].tv_sec) * 1000000LL + (end.tv_usec - start[acket.ackNum].tv_usec);
                    data->total_rtt += rtt;
                    data->total_messages++;
                }
            }
        }

        //TimeOut

        if (acketOn == 0)
        {
            nextSeq = base;
        }
        else
        {
            acketOn = 0;
        }
        

        // Break after a fixed number of messages
        if (data->total_messages >= num_requests) break; // Example condition
    }

    // Update request rate
    data->request_rate = (float) data->total_messages * 1000000 / (float) data->total_rtt;

    printf("Total RTT: %lld s\n", data->total_rtt / 1000000);
    printf("Total messages: %ld\n", data->total_messages);
    printf("RPS: %f\n", data->request_rate);

    close(data->socket_fd);
    close(data->epoll_fd);
    return NULL;
}

void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Create client threads
    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].epoll_fd = epoll_create1(0);
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        connect(thread_data[i].socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    }

    for (int i = 0; i < num_client_threads; i++) {
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    // Wait for threads to complete
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0;

    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
    }

    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr, clientAddress;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);
    int length;
    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    //listen(server_fd, 128);

    aClient ourClients[maxClients];
    int howManyClients = 0;

    int epoll_fd = epoll_create1(0);
    struct epoll_event event, events[MAX_EVENTS];
    int test = 1;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);
    packet recivedPacket;
    packet acketOut = {.isAck = 1};

    while (1) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n_events; i++) {
            int client_fd = events[i].data.fd;

            int n = recvfrom(client_fd, &recivedPacket, sizeof(recivedPacket), 0, (struct sockaddr*)&clientAddress, &length);

            aClient *thisIs;
            int preExistingClient = 0;
            for (size_t i = 0; i < howManyClients; i++)
            {
                if (ourClients[i].theAddress.sin_addr.s_addr == clientAddress.sin_addr.s_addr && ourClients[i].theAddress.sin_port == clientAddress.sin_port)
                {
                    thisIs = &ourClients[i];
                    preExistingClient = 1;
                }
                
            }
            int clientFull = 0;
            if (preExistingClient == 0)
            {
                if (howManyClients == maxClients)
                {
                    //printf("Too Many Clients");
                    clientFull == 1;
                }
                else
                {
                    //printf("New Client #%d!\n", howManyClients);
                    ourClients[howManyClients].theAddress = clientAddress;
                    ourClients[howManyClients].nextSeq = 0;
                    thisIs = &ourClients[howManyClients];
                    howManyClients++;
                }
            }
            
            
            if (n <= 0) {
                close(client_fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
            } else {
                if (clientFull == 0)
                {
                    if (thisIs->nextSeq == recivedPacket.seqNum)
                    {
                        //printf("Event %d\n", test);
                        test++;
                        acketOut.ackNum = recivedPacket.seqNum;
                        sendto(client_fd, &acketOut, sizeof(acketOut), 0, (struct sockaddr*)&clientAddress, sizeof(clientAddress));
                        thisIs->nextSeq++;
                    }
                    else
                    {
                        //printf("Not Right! %d When looking for %d \n", recivedPacket.seqNum, thisIs->nextSeq);
                    }
                    
                }
            }
            //}
        }
    }

    close(server_fd);
    close(epoll_fd);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) maxClients = atoi(argv[4]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);
        if (argc > 6) windowSize = atoi(argv[6]);
        

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port (max_clients|num_client_threads) num_requests window_size]\n", argv[0]);
    }

    return 0;
}